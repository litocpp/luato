export module luato:binding;

export import :error;

using namespace rstd::prelude;

export namespace luato
{

class State;

class CallFrame {
public:
    CallFrame(const CallFrame&)            = delete;
    CallFrame& operator=(const CallFrame&) = delete;

    auto argument_count() const noexcept -> usize { return argument_count_; }

    template<typename T>
        requires(rstd::mtp::same_as<T, i64> || rstd::mtp::same_as<T, String>)
    auto required(usize index) -> Result<T> {
        if constexpr (rstd::mtp::same_as<T, i64>) {
            return read_i64_(context_, index);
        } else {
            return read_string_(context_, index);
        }
    }

    void push(i64 value) { push_i64_(context_, value); }
    void push(bool value) { push_bool_(context_, value); }
    void push(ref<str> value) { push_string_(context_, String::make(value)); }
    void push(String value) { push_string_(context_, rstd::move(value)); }

private:
    using ReadI64    = auto (*)(void*, usize) -> Result<i64>;
    using ReadString = auto (*)(void*, usize) -> Result<String>;
    using PushI64    = void (*)(void*, i64);
    using PushBool   = void (*)(void*, bool);
    using PushString = void (*)(void*, String);

    CallFrame(void*      context,
              usize      argument_count,
              ReadI64    read_i64,
              ReadString read_string,
              PushI64    push_i64,
              PushBool   push_bool,
              PushString push_string) noexcept
        : context_(context),
          argument_count_(argument_count),
          read_i64_(read_i64),
          read_string_(read_string),
          push_i64_(push_i64),
          push_bool_(push_bool),
          push_string_(push_string) {}

    void*      context_;
    usize      argument_count_;
    ReadI64    read_i64_;
    ReadString read_string_;
    PushI64    push_i64_;
    PushBool   push_bool_;
    PushString push_string_;

    friend class State;
};

using NativeCallback = dyn<FnMut<BindingResult(CallFrame&)>>;

class NativeFunctionSpec {
public:
    NativeFunctionSpec(NativeFunctionSpec&&) noexcept            = default;
    NativeFunctionSpec& operator=(NativeFunctionSpec&&) noexcept = default;

    template<typename Callback>
    static auto make(String name, usize arity, Callback&& callback) -> NativeFunctionSpec {
        return NativeFunctionSpec(
            rstd::move(name), arity, Box<NativeCallback>::make(rstd::forward<Callback>(callback)));
    }

private:
    NativeFunctionSpec(String name, usize arity, Box<NativeCallback> callback)
        : name_(rstd::move(name)), arity_(arity), callback_(rstd::move(callback)) {}

    String              name_;
    usize               arity_;
    Box<NativeCallback> callback_;

    friend class State;
};

class ModuleSpec {
public:
    explicit ModuleSpec(String name)
        : name_(rstd::move(name)), functions_(Vec<NativeFunctionSpec>::make()) {}

    ModuleSpec(ModuleSpec&&) noexcept            = default;
    ModuleSpec& operator=(ModuleSpec&&) noexcept = default;

    void add(NativeFunctionSpec function) { functions_.push(rstd::move(function)); }

private:
    String                  name_;
    Vec<NativeFunctionSpec> functions_;

    friend class State;
};

} // namespace luato
