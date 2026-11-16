import luato;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::Arc;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace {
struct Checks {
  int failures{};
  void expect(bool value, const char *message) {
    if (!value) {
      ++failures;
      __builtin_printf("FAIL memory: %s\n", message);
    }
  }
};

auto source(ref<str> text) -> luato::LuaModuleSource {
  return {"memory"_Str, "memory:test"_Str, "memory.lua"_Str,
          Vec<u8>::from(text.as_bytes())};
}

void expect_closed(Checks &checks, const luato::MemoryObserver &memory) {
  auto stats = memory.snapshot();
  checks.expect(stats.closed, "observer must not retain the VM");
  checks.expect(stats.used_bytes == usize() &&
                    stats.live_allocations == usize(),
                "closing must release all Lua requests");
  checks.expect(stats.allocations == stats.deallocations,
                "closing must balance successful allocations");
  if (stats.pooled) {
    checks.expect(stats.pool->live_bytes == usize() &&
                      stats.pool->reserved_bytes == usize() &&
                      stats.upstream->mapped_bytes == usize(),
                  "closing must release pool slabs and mappings");
    checks.expect(stats.control_bytes > usize(),
                  "observer retains only the separate memory account");
  }
}

void system_mode(Checks &checks) {
  auto memory = [&] {
    auto state = luato::State::create(luato::StateOptions::base()).unwrap();
    auto stats = state.memory_statistics().unwrap();
    checks.expect(!stats.pooled && !stats.closed && stats.pool.is_none() &&
                      stats.upstream.is_none(),
                  "default mode must retain system allocation semantics");
    checks.expect(stats.used_bytes == state.used_memory().unwrap(),
                  "used_memory reports logical bytes");
    checks.expect(state.trim_memory().unwrap().bytes == usize(),
                  "system trim must not claim to release pool memory");
    return state.memory_observer().unwrap();
  }();
  expect_closed(checks, memory);
}

void pooled_lifetime(Checks &checks) {
  Option<luato::TableHandle> orphan;
  auto memory = [&] {
    auto state = luato::State::create(luato::StateOptions::build_script(),
                                      luato::MemoryOptions::pooled())
                     .unwrap();
    auto observer = state.memory_observer().unwrap();
    auto initial = observer.snapshot();
    checks.expect(initial.pooled && initial.used_bytes > usize() &&
                      initial.pool->live_bytes == initial.used_bytes &&
                      initial.upstream->mapped_bytes ==
                          initial.pool->reserved_bytes,
                  "Lua logical requests and page-rounded pool statistics");
    orphan = Some(state.create_table().unwrap());
    auto moved = rstd::move(state);
    checks.expect(state.memory_statistics().is_err() &&
                      state.memory_observer().is_err() &&
                      state.collect_garbage().is_err() &&
                      state.trim_memory().is_err(),
                  "moved-from State memory methods must fail");
    checks.expect(!observer.snapshot().closed && moved.used_memory().is_ok(),
                  "moving must not close or duplicate the VM");
    return observer;
  }();
  expect_closed(checks, memory);
  checks.expect(orphan->get_value("missing"_str).is_err(),
                "observer must not keep orphan handles valid");
  (void)orphan.take();
}

void limits_and_coroutines(Checks &checks) {
  auto first =
      luato::State::create(luato::StateOptions::build_script().with_coroutine(),
                           luato::MemoryOptions::pooled())
          .unwrap();
  auto second = luato::State::create(luato::StateOptions::build_script(),
                                     luato::MemoryOptions::pooled())
                    .unwrap();
  auto initial = first.used_memory().unwrap();
  auto second_initial = second.memory_statistics().unwrap();
  first.set_memory_limit(initial + usize(16384)).unwrap();
  auto coroutine = first.execute_entry(source(R"LUA(
    local co = coroutine.create(function() return string.rep('x', 1048576) end)
    local ok = coroutine.resume(co)
    assert(not ok)
  )LUA"_str));
  checks.expect(coroutine.is_ok(),
                "coroutine must share the main VM memory limit");
  auto rejected = first.memory_statistics().unwrap();
  checks.expect(rejected.limit_failures > usize() &&
                    rejected.used_bytes <= rejected.limit_bytes,
                "limit failures must not charge failed allocation bytes");
  auto unchanged = second.memory_statistics().unwrap();
  checks.expect(unchanged.used_bytes == second_initial.used_bytes &&
                    unchanged.upstream->mapped_bytes ==
                        second_initial.upstream->mapped_bytes,
                "VM accounts and pools must be independent");
  checks.expect(
      second.execute_entry(source("payload = string.rep('y', 1048576)"_str))
          .is_ok(),
      "another VM remains unrestricted");
  checks.expect(first.set_memory_limit(usize()).unwrap() ==
                    initial + usize(16384),
                "limit setter must return the previous request-byte limit");
  checks.expect(
      first.execute_entry(source("payload = string.rep('x', 1048576)"_str))
          .is_ok(),
      "VM must recover after removing its limit");
}

void collection_and_trim(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::build_script(),
                                    luato::MemoryOptions::pooled())
                   .unwrap();
  state.execute_entry(source("payload = string.rep('x', 1048576)"_str))
      .unwrap();
  auto peak = state.memory_statistics().unwrap();
  state.trim_memory().unwrap();
  checks.expect(state.used_memory().unwrap() == peak.used_bytes,
                "trim must not collect reachable Lua objects");
  state.execute_entry(source("assert(#payload == 1048576); payload = nil"_str))
      .unwrap();
  state.collect_garbage().unwrap();
  state.collect_garbage().unwrap();
  auto collected = state.memory_statistics().unwrap();
  checks.expect(collected.used_bytes + usize(1048576) < peak.used_bytes,
                "GC must release unreachable large strings");
  auto trim = state.trim_memory().unwrap();
  auto trimmed = state.memory_statistics().unwrap();
  checks.expect(trimmed.pool->retained_bytes == usize() &&
                    trimmed.upstream->mapped_bytes + trim.bytes ==
                        collected.upstream->mapped_bytes,
                "trim must release only empty page-rounded slabs");
  checks.expect(state.execute_entry(source("assert(6 * 7 == 42)"_str)).is_ok(),
                "trim must preserve the running VM");
}

void reuse_and_reentry(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::build_script(),
                                    luato::MemoryOptions::pooled())
                   .unwrap();
  luato::ModuleSpec host("host"_Str);
  host.function("check_gc"_Str,
                [&state] { return state.collect_garbage().is_err(); });
  state.register_module(rstd::move(host)).unwrap();
  state.execute_entry(source("assert(host.check_gc())"_str)).unwrap();
  auto work =
      source("local t = {}; for i=1,1000 do t[i]={i, tostring(i)} end"_str);
  state.execute_entry(rstd::move(work)).unwrap();
  state.collect_garbage().unwrap();
  auto before = state.memory_statistics().unwrap();
  for (unsigned i = 0; i < 4; ++i) {
    state
        .execute_entry(source(
            "local t = {}; for j=1,1000 do t[j]={j, tostring(j)} end"_str))
        .unwrap();
    state.collect_garbage().unwrap();
  }
  auto after = state.memory_statistics().unwrap();
  checks.expect(after.pool->reuses > before.pool->reuses &&
                    after.pool->reuses > after.upstream->allocation_calls,
                "temporary Lua tables must reuse pool blocks");
  checks.expect(after.pool->retained_bytes <= usize(65536),
                "per-VM empty retention must remain bounded");
}

void failed_creation(Checks &checks) {
  auto options = luato::MemoryOptions::pooled();
  options.pool.slab_granularity = usize(3);
  checks.expect(
      luato::State::create(luato::StateOptions::base(), options).is_err(),
      "invalid pool alignment must be rejected");
  options = luato::MemoryOptions::pooled();
  options.pool.slab_bytes = usize::MAX;
  options.pool.target_blocks_per_slab = usize();
  options.pool.max_pooled_block_bytes = usize(128);
  auto failed =
      luato::State::create(luato::StateOptions::build_script(), options);
  checks.expect(failed.is_err(),
                "partial VM initialization must recover from pool OOM");
  if (failed.is_err())
    checks.expect(failed.unwrap_err().kind == luato::ErrorKind::StateCreation,
                  "allocation failure must preserve state creation error");
  auto recovered = luato::State::create(luato::StateOptions::base(),
                                        luato::MemoryOptions::pooled());
  checks.expect(recovered.is_ok(),
                "another VM can start after initialization OOM");
}

void finalizers(Checks &checks) {
  for (unsigned limited = 0; limited < 2; ++limited) {
    int finalized{};
    auto memory = [&] {
      auto state = luato::State::create(luato::StateOptions::build_script(),
                                        luato::MemoryOptions::pooled())
                       .unwrap();
      luato::ModuleSpec host("host"_Str);
      host.function("finalize"_Str, [&finalized] {
        ++finalized;
        return "finalizer allocated a Lua string"_Str;
      });
      state.register_module(rstd::move(host)).unwrap();
      state
          .execute_entry(source(R"LUA(
      keep = setmetatable({}, {__gc=function() assert(#host.finalize() > 0) end})
    )LUA"_str))
          .unwrap();
      if (limited)
        state.set_memory_limit(usize(1)).unwrap();
      return state.memory_observer().unwrap();
    }();
    checks.expect(
        finalized == 1,
        "Lua close must run allocating finalizers before pool shutdown");
    if (limited)
      checks.expect(
          memory.snapshot().limit_failures > usize(),
          "close must recover when finalizer allocation hits its limit");
    expect_closed(checks, memory);
  }
}

struct Gate {
  bool entered{};
  bool dropped{};
};
auto wait_at_gate(Gate &gate) -> rstd::async::coro<luato::Result<i64>> {
  struct Drop {
    Gate &gate;
    ~Drop() { gate.dropped = true; }
  } drop{gate};
  gate.entered = true;
  for (;;)
    co_await rstd::async::yield_now();
  co_return Ok(i64());
}
auto cancel_after_owner_drop(Option<luato::State> &state,
                             luato::FunctionHandle &wait,
                             const luato::MemoryObserver &memory, Gate &gate)
    -> rstd::async::coro<bool> {
  auto task =
      rstd::async::spawn_local(wait.call_async(luato::MultiValue::make()));
  while (!gate.entered && !task.is_finished())
    co_await rstd::async::yield_now();
  if (!gate.entered) {
    (void)co_await rstd::move(task);
    co_return false;
  }
  (void)state.take();
  bool retained = !memory.snapshot().closed;
  task.abort();
  auto result = co_await rstd::move(task);
  co_return retained &&result.is_err() && gate.dropped;
}

void suspended_lifetime(Checks &checks) {
  auto runtime = rstd::async::RuntimeBuilder::current_thread()
                     .enable_all()
                     .build()
                     .unwrap();
  Option<luato::State> state =
      Some(luato::State::create(luato::StateOptions::build_script(),
                                luato::MemoryOptions::pooled())
               .unwrap());
  auto memory = state->memory_observer().unwrap();
  Gate gate;
  luato::ModuleSpec host("host"_Str);
  host.async_function("wait"_Str, [&gate] { return wait_at_gate(gate); });
  state->register_module(rstd::move(host)).unwrap();
  auto globals = state->globals().unwrap();
  auto table = globals.get_table("host"_str).unwrap();
  auto wait = table.get_function("wait"_str).unwrap();
  checks.expect(
      runtime.block_on(rstd::async::into_future(
          cancel_after_owner_drop(state, wait, memory, gate))),
      "suspended invocation must own the VM until cancellation cleanup");
  expect_closed(checks, memory);
  checks.expect(wait.call_values(luato::MultiValue::make()).is_err(),
                "cancelled VM handles must become orphaned");
}

void concurrent_observer(Checks &checks) {
  auto state = luato::State::create(luato::StateOptions::build_script(),
                                    luato::MemoryOptions::pooled())
                   .unwrap();
  auto memory = state.memory_observer().unwrap();
  auto stop = Arc<Atomic<bool>>::make(false);
  auto reader =
      rstd::thread::spawn([memory = memory.clone(), stop = stop.clone()] {
        bool valid = true;
        while (!stop->load(Ordering::Acquire)) {
          auto stats = memory.snapshot();
          if (stats.used_bytes > stats.peak_used_bytes ||
              stats.pool->live_bytes != stats.used_bytes ||
              stats.pool->reserved_bytes != stats.upstream->mapped_bytes)
            valid = false;
          (void)memory.trim();
          rstd::thread::yield_now();
        }
        return valid;
      }).unwrap();
  auto result = state.execute_entry(source(
      "for i=1,2000 do local t={i, tostring(i), string.rep('a', i%100)} end"_str));
  stop->store(true, Ordering::Release);
  checks.expect(rstd::move(reader).join().unwrap(),
                "observer snapshots and trim are synchronized");
  checks.expect(result.is_ok(),
                "observer must not enter or disrupt the Lua VM");
}
} // namespace

auto expect_memory_contract() -> int {
  Checks checks;
  system_mode(checks);
  pooled_lifetime(checks);
  limits_and_coroutines(checks);
  collection_and_trim(checks);
  reuse_and_reentry(checks);
  failed_creation(checks);
  finalizers(checks);
  suspended_lifetime(checks);
  concurrent_observer(checks);
  __builtin_printf("Lua memory: 9 scenarios, %d failures\n", checks.failures);
  return checks.failures == 0 ? 0 : 1;
}
