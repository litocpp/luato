export module luato:userdata;

export import :state;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::rc::make_rc;
using alloc::rc::Rc;

export namespace luato {

template <typename T> struct Variadic {
  Vec<T> values;
};

enum class AsyncMethodErrorKind { Arguments, Execution, Return };

template <typename T> struct FromLua {
  static auto from_lua(LuaValue value) -> Result<T> {
    if constexpr (rstd::mtp::same_as<T, LuaValue>)
      return Ok(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, bool>) {
      if (value.is_Boolean())
        return Ok(value.as_Boolean().value);
    } else if constexpr (rstd::mtp::same_as<T, i64>) {
      if (value.is_Integer())
        return Ok(value.as_Integer().value);
    } else if constexpr (rstd::mtp::same_as<T, f64>) {
      if (value.is_Number())
        return Ok(value.as_Number().value);
      if (value.is_Integer())
        return Ok(f64(value.as_Integer().value.to_primitive()));
    } else if constexpr (rstd::mtp::same_as<T, String>) {
      if (value.is_Bytes()) {
        auto result = String::from_utf8(rstd::move(value.as_Bytes().value));
        if (result.is_ok())
          return Ok(rstd::move(*result));
      }
    } else if constexpr (rstd::mtp::same_as<T, Vec<u8>>) {
      if (value.is_Bytes())
        return Ok(rstd::move(value.as_Bytes().value));
    } else if constexpr (rstd::mtp::same_as<T, TableHandle>) {
      if (value.is_Table())
        return Ok(rstd::move(value.as_Table().value));
    } else if constexpr (rstd::mtp::same_as<T, FunctionHandle>) {
      if (value.is_Function())
        return Ok(rstd::move(value.as_Function().value));
    } else if constexpr (rstd::mtp::same_as<T, UserDataHandle>) {
      if (value.is_UserData())
        return Ok(rstd::move(value.as_UserData().value));
    } else
      static_assert(!rstd::mtp::same_as<T, T>, "unsupported Lua conversion");
    return Err(Error::make(ErrorKind::Type, String::make(),
                           "Lua argument type mismatch"_Str));
  }
};

template <typename T> struct FromLua<Option<T>> {
  static auto from_lua(LuaValue value) -> Result<Option<T>> {
    if (value.is_Nil())
      return Ok(None());
    auto result = FromLua<T>::from_lua(rstd::move(value));
    if (result.is_err())
      return Err(rstd::move(result).unwrap_err());
    return Ok(Some(rstd::move(*result)));
  }
};

template <typename T> struct IntoLua {
  static auto into_lua(T value) -> LuaValue {
    if constexpr (rstd::mtp::same_as<T, LuaValue>)
      return rstd::move(value);
    else if constexpr (rstd::mtp::same_as<T, bool>)
      return LuaValue::Boolean(value);
    else if constexpr (rstd::mtp::same_as<T, i64>)
      return LuaValue::Integer(value);
    else if constexpr (rstd::mtp::same_as<T, f64>)
      return LuaValue::Number(value);
    else if constexpr (rstd::mtp::same_as<T, String>)
      return LuaValue::Bytes(rstd::move(value).into_bytes());
    else if constexpr (rstd::mtp::same_as<T, ref<str>>)
      return LuaValue::Bytes(Vec<u8>::from(value.as_bytes()));
    else if constexpr (rstd::mtp::same_as<T, Vec<u8>>)
      return LuaValue::Bytes(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, TableHandle>)
      return LuaValue::Table(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, FunctionHandle>)
      return LuaValue::Function(rstd::move(value));
    else if constexpr (rstd::mtp::same_as<T, UserDataHandle>)
      return LuaValue::UserData(rstd::move(value));
    else
      static_assert(!rstd::mtp::same_as<T, T>, "unsupported Lua conversion");
  }
};

template <typename T> struct IntoLua<Option<T>> {
  static auto into_lua(Option<T> value) -> LuaValue {
    if (value.is_none())
      return LuaValue::Nil();
    return IntoLua<T>::into_lua(rstd::move(*value));
  }
};

template <typename T>
auto argument(CallFrame &frame, usize index) -> Result<T> {
  auto value = frame.argument(index);
  if (value.is_err())
    return Err(rstd::move(value).unwrap_err());
  return FromLua<T>::from_lua(take_lua_value(*value));
}

inline auto return_values(CallFrame &frame, MultiValue values)
    -> BindingResult {
  for (auto &value : rstd::move(values).into_values())
    frame.return_value(OpaqueHandle{new LuaValue(rstd::move(value))});
  return Ok(usize());
}

template <typename T>
auto return_value(CallFrame &frame, T value) -> BindingResult {
  if constexpr (!rstd::mtp::same_as<T, empty>)
    frame.return_value(
        OpaqueHandle{new LuaValue(IntoLua<T>::into_lua(rstd::move(value)))});
  return Ok(usize());
}

template <typename T>
auto return_value(CallFrame &frame, Result<T> value) -> BindingResult {
  if (value.is_err())
    return Err(rstd::move(value).unwrap_err());
  return return_value(frame, rstd::move(*value));
}

template <typename R>
auto from_multi(MultiValue values, ReturnOptions options = {}) -> Result<R> {
  if constexpr (rstd::mtp::same_as<R, MultiValue>)
    return Ok(rstd::move(values));
  else if constexpr (rstd::mtp::same_as<R, IgnoreReturns>)
    return Ok(IgnoreReturns{});
  else if constexpr (rstd::mtp::same_as<R, empty>) {
    if (values.is_empty())
      return Ok(empty{});
  } else if (values.len() == usize(1)) {
    auto entries = rstd::move(values).into_values();
    auto result = FromLua<R>::from_lua(rstd::move(entries[usize()]));
    if (result.is_err() && options.conversion_error.is_some())
      return Err(rstd::move(*options.conversion_error));
    return result;
  }
  if (options.conversion_error.is_some())
    return Err(rstd::move(*options.conversion_error));
  return Err(Error::make(ErrorKind::Type, String::make(),
                         "unexpected Lua return value count"_Str));
}

template <typename R, typename... Args>
auto FunctionHandle::call(Args... args) -> Result<R> {
  return call<R>(ReturnOptions{}, rstd::move(args)...);
}

template <typename R, typename... Args>
auto FunctionHandle::call(ReturnOptions options, Args... args) -> Result<R> {
  auto values = MultiValue::make();
  (values.push(IntoLua<Args>::into_lua(rstd::move(args))), ...);
  auto result = call_values_impl(rstd::move(values),
                                 rstd::mtp::same_as<R, IgnoreReturns>);
  if (result.is_err())
    return Err(rstd::move(result).unwrap_err());
  return from_multi<R>(rstd::move(*result), rstd::move(options));
}

template <typename R, typename... Args>
auto FunctionHandle::call_async_typed(Args... args)
    -> rstd::async::coro<Result<R>> {
  return call_async_typed<R>(ReturnOptions{}, rstd::move(args)...);
}

template <typename R, typename... Args>
auto FunctionHandle::call_async_typed(ReturnOptions options, Args... args)
    -> rstd::async::coro<Result<R>> {
  auto values = MultiValue::make();
  (values.push(IntoLua<Args>::into_lua(rstd::move(args))), ...);
  auto task = call_async_owned(rstd::move(values),
                               rstd::mtp::same_as<R, IgnoreReturns>);
  // The task owns a cloned function before suspension.
  return [](rstd::async::coro<Result<MultiValue>> task,
            ReturnOptions options) -> rstd::async::coro<Result<R>> {
    auto result = co_await rstd::move(task);
    if (result.is_err())
      co_return Err(rstd::move(result).unwrap_err());
    co_return from_multi<R>(rstd::move(*result), rstd::move(options));
  }(rstd::move(task), rstd::move(options));
}

} // namespace luato

namespace luato {

template <typename T, typename Signature> struct UserDataMethod;

template <typename T> struct OptionalArgument {
  static constexpr bool value = false;
};
template <typename T> struct OptionalArgument<Option<T>> {
  static constexpr bool value = true;
};
template <typename T> struct VariadicArgument {
  static constexpr bool value = false;
};
template <typename T> struct VariadicArgument<Variadic<T>> {
  static constexpr bool value = true;
  using type = T;
};
struct IgnoreAsyncMethodError {
  void operator()(const UserDataHandle &, const Error &,
                  AsyncMethodErrorKind) const {}
};

template <typename T, typename Signature> struct UserDataAsyncMethod;
template <typename T, typename C, typename Self, typename... Args>
struct UserDataAsyncMethod<T, C(Self, Args...)> {
  static_assert(rstd::mtp::same_as<Self, UserDataHandle>,
                "async userdata methods own their userdata handle");
  static_assert((!rstd::mtp::is_ref<Args> && ...),
                "async userdata arguments must be owned");
  using R = typename AsyncReturnOf<C>::type;
  using Arguments = rstd::tuple<Option<Args>...>;

  template <typename F, typename Failure> struct Method {
    F function;
    Failure failure;
  };
  template <rstd::size_t I = 0>
  static auto read(CallFrame &frame, Arguments &arguments) -> Result<empty> {
    if constexpr (I == sizeof...(Args)) {
      if (frame.argument_count() > usize(sizeof...(Args) + 1) &&
          !(VariadicArgument<Args>::value || ...))
        return Err(
            Error::make(ErrorKind::Type, String::make(),
                        "unexpected userdata method argument count"_Str));
      return Ok(empty{});
    } else {
      using Arg =
          typename rstd::mtp::func_traits<void(Args...)>::template argument<I>;
      if constexpr (VariadicArgument<Arg>::value) {
        static_assert(I + 1 == sizeof...(Args),
                      "variadic argument must be last");
        Arg values;
        for (auto index = usize(I + 1); index < frame.argument_count();
             ++index) {
          auto value =
              argument<typename VariadicArgument<Arg>::type>(frame, index);
          if (value.is_err())
            return Err(rstd::move(value).unwrap_err());
          values.values.push(rstd::move(*value));
        }
        rstd::get<I>(arguments) = Some(rstd::move(values));
      } else if (frame.argument_count() <= usize(I + 1)) {
        if constexpr (OptionalArgument<Arg>::value)
          rstd::get<I>(arguments) = Some(Arg{});
        else
          return Err(
              Error::make(ErrorKind::Type, String::make(),
                          "unexpected userdata method argument count"_Str));
      } else {
        auto value = argument<Arg>(frame, usize(I + 1));
        if (value.is_err())
          return Err(rstd::move(value).unwrap_err());
        rstd::get<I>(arguments) = Some(rstd::move(*value));
      }
      return read<I + 1>(frame, arguments);
    }
  }
  static auto finish(R value) -> AsyncReturn {
    if constexpr (rstd::mtp::same_as<R, AsyncReturn>)
      return rstd::move(value);
    else if constexpr (rstd::mtp::same_as<R, MultiValue>)
      return AsyncReturn::values(rstd::move(value));
    else
      return AsyncReturn::make(
          [value = rstd::move(value)](CallFrame &frame) mutable {
            if constexpr (!rstd::mtp::same_as<R, empty>) {
              auto output = IntoLua<R>::into_lua(rstd::move(value));
              auto valid = frame.validate_value(output);
              if (valid.is_err())
                return BindingResult(Err(rstd::move(valid).unwrap_err()));
              return return_value(frame, rstd::move(output));
            } else {
              return return_value(frame, rstd::move(value));
            }
          });
  }
  template <typename M, rstd::size_t... I>
  static auto run(Rc<M> method, UserDataHandle self, Arguments arguments,
                  rstd::mtp::index_sequence<I...>)
      -> rstd::async::coro<Result<AsyncReturn>> {
    auto receiver = self.clone();
    if (receiver.is_err()) {
      method.get()->failure(self, receiver.as_ref().unwrap_err(),
                            AsyncMethodErrorKind::Arguments);
      co_return Err(rstd::move(receiver).unwrap_err());
    }
    auto result = co_await method.get()->function(
        rstd::move(*receiver), rstd::move(*rstd::get<I>(arguments))...);
    if (result.is_err()) {
      method.get()->failure(self, result.as_ref().unwrap_err(),
                            AsyncMethodErrorKind::Execution);
      co_return Err(rstd::move(result).unwrap_err());
    }
    co_return Ok(
        rstd::move(finish(rstd::move(*result)))
            .on_error([method = rstd::move(method),
                       self = rstd::move(self)](const Error &error) mutable {
              method.get()->failure(self, error, AsyncMethodErrorKind::Return);
            }));
  }
  template <typename F, typename Failure>
  static auto wrap(F &&function, Failure &&failure) {
    using M = Method<rstd::mtp::decay<F>, rstd::mtp::decay<Failure>>;
    auto method = make_rc<M>(
        M{rstd::forward<F>(function), rstd::forward<Failure>(failure)});
    return [method = rstd::move(method)](CallFrame &frame) mutable
               -> Result<rstd::async::coro<Result<AsyncReturn>>> {
      auto self = argument<UserDataHandle>(frame, usize());
      if (self.is_err())
        return Err(rstd::move(self).unwrap_err());
      auto arguments = Arguments{};
      auto loaded = read(frame, arguments);
      if (loaded.is_err()) {
        method.get()->failure(*self, loaded.as_ref().unwrap_err(),
                              AsyncMethodErrorKind::Arguments);
        return Err(rstd::move(loaded).unwrap_err());
      }
      return Ok(run(method.clone(), rstd::move(*self), rstd::move(arguments),
                    rstd::mtp::make_index_sequence<sizeof...(Args)>{}));
    };
  }
};

template <typename T, typename R, typename Self, typename... Args>
struct UserDataMethod<T, R(Self, Args...)> {
  static_assert(rstd::mtp::same_as<Self, const T &> ||
                rstd::mtp::same_as<Self, T &>);
  using Arguments = rstd::tuple<Option<rstd::mtp::rm_cvf<Args>>...>;

  template <rstd::size_t I = 0>
  static auto read(CallFrame &frame, Arguments &arguments) -> Result<empty> {
    if constexpr (I == sizeof...(Args))
      return Ok(empty{});
    else {
      using Arg = rstd::mtp::rm_cvf<
          typename rstd::mtp::func_traits<R(Args...)>::template argument<I>>;
      static_assert(!rstd::mtp::is_ref<Arg>);
      auto value = argument<Arg>(frame, usize(I + 1));
      if (value.is_err())
        return Err(rstd::move(value).unwrap_err());
      rstd::get<I>(arguments) = Some(rstd::move(*value));
      return read<I + 1>(frame, arguments);
    }
  }
  template <typename F, rstd::size_t... I>
  static auto invoke(F &function, CallFrame &frame, Self self, Arguments &args,
                     rstd::mtp::index_sequence<I...>) -> BindingResult {
    if constexpr (rstd::mtp::is_void<R>) {
      function(self, rstd::move(*rstd::get<I>(args))...);
      return Ok(usize());
    } else
      return return_value(frame,
                          function(self, rstd::move(*rstd::get<I>(args))...));
  }
  template <typename F> static auto wrap(F &&function) {
    return [function = rstd::forward<F>(function)](
               CallFrame &frame) mutable -> BindingResult {
      if (frame.argument_count() != usize(sizeof...(Args) + 1))
        return Err(
            Error::make(ErrorKind::Type, String::make(),
                        "unexpected userdata method argument count"_Str));
      auto self = argument<UserDataHandle>(frame, usize());
      if (self.is_err())
        return Err(rstd::move(self).unwrap_err());
      auto args = Arguments{};
      auto loaded = read(frame, args);
      if (loaded.is_err())
        return Err(rstd::move(loaded).unwrap_err());
      auto call = [&](Self object) -> BindingResult {
        return invoke(function, frame, object, args,
                      rstd::mtp::make_index_sequence<sizeof...(Args)>{});
      };
      if constexpr (rstd::mtp::same_as<Self, T &>)
        return self->template with_mut<T>(call);
      else
        return self->template with<T>(call);
    };
  }
};

} // namespace luato

export namespace luato {

template <typename T> class UserDataSpec {
public:
  explicit UserDataSpec(String name) : methods_(rstd::move(name)) {}
  template <typename F> void raw_method(String name, F &&function) {
    methods_.raw_variadic_function(
        rstd::move(name),
        [function = rstd::forward<F>(function)](
            CallFrame &frame) mutable -> BindingResult {
          auto self = argument<UserDataHandle>(frame, usize());
          if (self.is_err())
            return Err(rstd::move(self).unwrap_err());
          auto matches = self->template is<T>();
          if (matches.is_err())
            return Err(rstd::move(matches).unwrap_err());
          if (!*matches)
            return Err(Error::make(ErrorKind::Type, String::make(),
                                   "native userdata type mismatch"_Str));
          return function(frame);
        });
  }
  template <typename F> void method(String name, F &&function) {
    using Signature = typename rstd::mtp::func_traits<F>::signature;
    raw_method(rstd::move(name),
               UserDataMethod<T, Signature>::wrap(rstd::forward<F>(function)));
  }
  template <typename F> void raw_field_get(String name, F &&function) {
    field_names_.push(name.clone());
    raw_method(rstd::move(name), rstd::forward<F>(function));
  }
  template <typename F> void field_get(String name, F &&function) {
    using Traits = rstd::mtp::func_traits<F>;
    using Signature = typename Traits::signature;
    static_assert(Traits::arity == 1,
                  "userdata field getters only accept the receiver");
    static_assert(
        rstd::mtp::same_as<typename Traits::template argument<0>, const T &>,
        "userdata field getters borrow a const receiver");
    raw_field_get(rstd::move(name), UserDataMethod<T, Signature>::wrap(
                                        rstd::forward<F>(function)));
  }
  template <typename F> void raw_async_method(String name, F &&starter) {
    methods_.async_values_function(
        rstd::move(name), variadic_arity,
        [starter = rstd::forward<F>(starter)](
            CallFrame &frame) mutable -> decltype(starter(frame)) {
          auto self = argument<UserDataHandle>(frame, usize());
          if (self.is_err())
            return Err(rstd::move(self).unwrap_err());
          auto matches = self->template is<T>();
          if (matches.is_err())
            return Err(rstd::move(matches).unwrap_err());
          if (!*matches)
            return Err(Error::make(ErrorKind::Type, String::make(),
                                   "native userdata type mismatch"_Str));
          return starter(frame);
        });
  }

  template <typename F, typename Failure>
  void async_method(String name, F &&function, Failure &&failure) {
    using Signature = typename rstd::mtp::func_traits<F>::signature;
    raw_async_method(rstd::move(name), UserDataAsyncMethod<T, Signature>::wrap(
                                           rstd::forward<F>(function),
                                           rstd::forward<Failure>(failure)));
  }
  template <typename F> void async_method(String name, F &&function) {
    async_method(rstd::move(name), rstd::forward<F>(function),
                 IgnoreAsyncMethodError{});
  }

private:
  ModuleSpec methods_;
  Vec<String> field_names_;
  friend class State;
};

template <typename T>
auto State::register_userdata(UserDataSpec<T> spec) -> Result<empty> {
  return register_module_table(rstd::move(spec.methods_), None(), None(),
                               &userdata_type_key<T>,
                               rstd::move(spec.field_names_));
}

} // namespace luato
