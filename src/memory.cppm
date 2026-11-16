module;
#include <stddef.h>
export module luato:memory;
export import :error;
using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::alloc::Layout;
using rstd::alloc::PoolAllocator;
using rstd::alloc::VirtualMemoryAllocator;
using rstd::sync::Arc;
using rstd::sync::Mutex;

export namespace luato {
enum class MemoryKind { System, Pooled };

struct MemoryOptions {
  MemoryKind kind{MemoryKind::System};
  rstd::alloc::PoolOptions pool{.slab_bytes = usize(65536),
                                .max_pooled_block_bytes = usize(131072),
                                .retained_bytes = usize(65536),
                                .size_classes =
                                    rstd::alloc::PoolSizeClasses::Fine,
                                .target_blocks_per_slab = usize(4),
                                .min_slab_bytes = usize(4096)};

  static auto pooled() -> MemoryOptions {
    MemoryOptions options;
    options.kind = MemoryKind::Pooled;
    return options;
  }
};

struct MemoryStatistics {
  bool pooled{}, closed{};
  // Lua request bytes, excluding pool rounding and host-side objects.
  usize used_bytes{}, peak_used_bytes{}, limit_bytes{}, live_allocations{};
  usize allocations{}, deallocations{}, reallocations{}, failures{},
      limit_failures{};
  // The account mapping is separate from pool upstream mappings.
  usize control_bytes{};
  Option<rstd::alloc::PoolStatistics> pool;
  Option<rstd::alloc::VirtualMemoryStatistics> upstream;
};
} // namespace luato

namespace luato {
struct MemoryState {
  PoolAllocator<VirtualMemoryAllocator> pool;
  MemoryStatistics statistics;

  explicit MemoryState(MemoryOptions options, usize control_bytes)
      : pool(options.pool, VirtualMemoryAllocator{}) {
    statistics.pooled = options.kind == MemoryKind::Pooled;
    statistics.control_bytes = control_bytes;
  }
};
using MemoryOwner = Arc<Mutex<MemoryState>>;
class VmMemory;
} // namespace luato

export namespace luato {
// Snapshot and trim are synchronized without retaining or entering the Lua VM.
class MemoryObserver {
  friend class VmMemory;
  MemoryOwner owner_;
  explicit MemoryObserver(MemoryOwner owner) : owner_(rstd::move(owner)) {}

public:
  MemoryObserver(const MemoryObserver &) = delete;
  auto operator=(const MemoryObserver &) -> MemoryObserver & = delete;
  MemoryObserver(MemoryObserver &&) noexcept = default;
  auto operator=(MemoryObserver &&) noexcept -> MemoryObserver & = default;

  auto clone() const -> MemoryObserver {
    return MemoryObserver(owner_.clone());
  }
  auto snapshot() const -> MemoryStatistics {
    auto state = owner_->lock().unwrap_unchecked();
    auto result = state->statistics;
    if (result.pooled) {
      result.pool = Some(state->pool.statistics());
      result.upstream = Some(state->pool.upstream_statistics());
    }
    return result;
  }
  auto trim(usize retain = {}) const -> rstd::alloc::PoolTrimReport {
    auto state = owner_->lock().unwrap_unchecked();
    return state->statistics.pooled ? state->pool.trim(retain)
                                    : rstd::alloc::PoolTrimReport{};
  }
};
} // namespace luato

namespace luato {
class VmMemory {
  MemoryObserver observer_;
  explicit VmMemory(MemoryObserver observer)
      : observer_(rstd::move(observer)) {}

public:
  VmMemory(VmMemory &&) noexcept = default;
  auto operator=(VmMemory &&) noexcept -> VmMemory & = default;

  static auto create(MemoryOptions options) -> Result<VmMemory> {
    if (options.kind != MemoryKind::System &&
        options.kind != MemoryKind::Pooled)
      return Err(Error::binding("invalid Lua memory allocator"_Str));
    auto control = VirtualMemoryAllocator{};
    if (options.kind == MemoryKind::Pooled) {
      if (Layout::from_size_align(usize(), options.pool.slab_granularity)
              .is_none() ||
          (options.pool.size_classes != rstd::alloc::PoolSizeClasses::Fine &&
           options.pool.size_classes !=
               rstd::alloc::PoolSizeClasses::PowersOfTwo))
        return Err(Error::binding("invalid Lua pool options"_Str));
      options.pool.slab_granularity =
          options.pool.slab_granularity.max(control.page_size());
    }
    auto layout = MemoryOwner::allocation_layout_in<VirtualMemoryAllocator>();
    if (layout.is_none())
      return Err(Error::make(ErrorKind::Memory, String{},
                             "invalid Lua memory account layout"_Str));
    auto bytes = control.allocation_size(*layout);
    if (bytes.is_none())
      return Err(Error::make(ErrorKind::Memory, String{},
                             "invalid Lua memory account mapping"_Str));
    auto owner = MemoryOwner::try_make_in(rstd::move(control), options, *bytes);
    if (owner.is_err())
      return Err(Error::make(ErrorKind::Memory, String{},
                             "cannot allocate Lua memory account"_Str));
    return Ok(VmMemory(MemoryObserver(rstd::move(owner).unwrap())));
  }

  auto observer() const -> MemoryObserver { return observer_.clone(); }
  auto statistics() const -> MemoryStatistics { return observer_.snapshot(); }
  auto used_memory() const -> usize {
    return observer_.owner_->lock().unwrap_unchecked()->statistics.used_bytes;
  }
  auto set_limit(usize limit) -> usize {
    auto state = observer_.owner_->lock().unwrap_unchecked();
    return rstd::exchange(state->statistics.limit_bytes, limit);
  }
  void finish() {
    auto state = observer_.owner_->lock().unwrap_unchecked();
    state->statistics.closed = true;
    if (state->statistics.pooled)
      (void)state->pool.trim();
  }

  auto reallocate(void *pointer, usize old_size, usize new_size) -> void * {
    auto state = observer_.owner_->lock().unwrap_unchecked();
    auto &stats = state->statistics;
    if (!pointer)
      old_size = usize();
    auto alignment = usize(alignof(max_align_t));
    if (new_size == usize()) {
      if (pointer) {
        auto layout = Layout::from_size_align_unchecked(old_size, alignment);
        if (stats.pooled)
          state->pool.deallocate(pointer, layout);
        else
          ::alloc::dealloc(pointer, layout);
        stats.used_bytes -= old_size;
        --stats.live_allocations;
        ++stats.deallocations;
      }
      return nullptr;
    }
    auto base = stats.used_bytes - old_size;
    auto used = base.checked_add(new_size);
    if (used.is_none()) {
      ++stats.failures;
      return nullptr;
    }
    if (new_size > old_size && stats.limit_bytes != usize() &&
        *used > stats.limit_bytes) {
      ++stats.failures;
      ++stats.limit_failures;
      return nullptr;
    }
    auto layout = Layout::from_size_align(new_size, alignment);
    if (layout.is_none()) {
      ++stats.failures;
      return nullptr;
    }
    void *result;
    if (stats.pooled) {
      if (pointer) {
        auto old_layout =
            Layout::from_size_align_unchecked(old_size, alignment);
        auto resized = new_size >= old_size
                           ? state->pool.grow(pointer, old_layout, *layout)
                           : state->pool.shrink(pointer, old_layout, *layout);
        result = resized.is_ok() ? resized->pointer : nullptr;
      } else {
        auto allocated = state->pool.allocate(*layout);
        result = allocated.is_ok() ? allocated->pointer : nullptr;
      }
    } else if (pointer) {
      result = ::alloc::realloc(
          pointer, Layout::from_size_align_unchecked(old_size, alignment),
          new_size);
    } else {
      result = ::alloc::alloc(*layout);
    }
    if (!result) {
      ++stats.failures;
      return nullptr;
    }
    stats.used_bytes = *used;
    stats.peak_used_bytes = stats.peak_used_bytes.max(*used);
    if (pointer)
      ++stats.reallocations;
    else {
      ++stats.allocations;
      ++stats.live_allocations;
    }
    return result;
  }
};
} // namespace luato
