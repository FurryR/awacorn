#ifndef _AWACORN_EXPERIMENTAL_ASYNC
#define _AWACORN_EXPERIMENTAL_ASYNC
#include <tuple>
#include <type_traits>
#include <unordered_map>
#if __cplusplus >= 201101L
/**
 * Project Awacorn 基于 MIT 协议开源。
 * Copyright(c) 凌 2023.
 */
#include <memory>

#include "../detail/capture.hpp"
#include "../detail/function.hpp"
#include "../promise.hpp"
namespace awacorn {
namespace stmt {
template <typename>
struct value;
template <typename T>
using var = value<T*>;
};  // namespace stmt
namespace detail {
template <std::size_t... Is>
struct index_sequence {};
template <std::size_t I, std::size_t... Is>
struct make_index_sequence : make_index_sequence<I - 1, I - 1, Is...> {};
template <std::size_t... Is>
struct make_index_sequence<0, Is...> : index_sequence<Is...> {};

template <typename>
struct _is_value_impl : std::false_type {};
template <typename T>
struct _is_value_impl<stmt::value<T>> : std::true_type {};
template <typename T>
using is_value = _is_value_impl<typename std::decay<T>::type>;
template <typename T>
struct _value {
  promise<T> get() const noexcept { return pm; }
  promise<void> apply() {
    if (fn) {
      pm = fn();
      fn = function<promise<T>()>();
    }
    return pm.then([](const T&) {});
  }
  template <typename U,
            typename = typename std::enable_if<!std::is_same<
                typename std::decay<U>::type, _value<T>>::value>::type>
  _value(U&& _fn) : fn(std::forward<U>(_fn)){};
  _value(const _value&) = delete;
  _value& operator=(const _value&) = delete;

 private:
  function<promise<T>()> fn;
  promise<T> pm;
};
template <>
struct _value<void> {
  promise<void> get() const noexcept { return pm; }
  promise<void> apply() {
    if (fn) {
      pm = fn();
      fn = function<promise<void>()>();
    }
    return pm.then([]() {});
  }
  template <typename U,
            typename = typename std::enable_if<!std::is_same<
                typename std::decay<U>::type, _value<void>>::value>::type>
  _value(U&& _fn) : fn(std::forward<U>(_fn)){};
  _value(const _value&) = delete;
  _value& operator=(const _value&) = delete;

 private:
  function<promise<void>()> fn;
  promise<void> pm;
};
};  // namespace detail
template <typename T>
struct context;
namespace stmt {
template <typename Ctx>
struct expr {
  template <typename>
  friend struct awacorn::context;
  promise<void> apply(context<Ctx>& ctx) {
    if (fn) {
      return fn(ctx);
    } else
      return resolve();
  }
  template <typename U,
            typename = typename std::enable_if<!std::is_same<
                typename std::decay<U>::type, expr<Ctx>>::value>::type>
  expr(U&& _fn) : fn(std::forward<U>(_fn)){};
  expr(const expr&) = delete;
  expr(expr&& v) : fn(std::move(v.fn)) {}
  detail::function<promise<void>(context<Ctx>&)> fn;
};
class _value_get_impl {
  template <
      std::size_t I, std::size_t... Is, typename U, typename T,
      typename... Args, typename... Args2,
      typename = typename std::enable_if<!detail::is_value<T>::value>::type>
  static auto _apply(const std::shared_ptr<std::tuple<Args2...>>& ptr,
                     detail::index_sequence<Is...>, U&& fn, T&& v,
                     Args&&... args)
      -> promise<decltype(fn(std::declval<Args2>()...))> {
    std::get<I>(*ptr) = std::forward<T>(v);
    return _apply<I + 1>(ptr,
                         detail::make_index_sequence < sizeof...(Args) == 0
                             ? 0
                             : sizeof...(Args) - 1 > (),
                         std::forward<U>(fn), std::forward<Args>(args)...);
  }
  template <std::size_t I, std::size_t... Is, typename U, typename T,
            typename... Args, typename... Args2>
  static auto _apply(const std::shared_ptr<std::tuple<Args2...>>& ptr,
                     detail::index_sequence<Is...>, U&& fn, const value<T>& v,
                     Args&&... args)
      -> promise<decltype(fn(std::declval<Args2>()...))> {
    auto _fn = detail::capture(std::forward<U>(fn));
    auto _args = detail::capture(std::make_tuple(std::forward<Args>(args)...));
    return v.apply().then([ptr, v, _fn, _args]() mutable {
      return v.get().then([ptr, v, _fn, _args](const T& x) mutable {
        auto&& args = _args.borrow();
        std::get<I>(*ptr) = x;
        return _apply<I + 1>(ptr,
                             detail::make_index_sequence < sizeof...(Args) == 0
                                 ? 0
                                 : sizeof...(Args) - 1 > (),
                             std::move(_fn.borrow()),
                             std::get<Is>(std::move(args))...);
      });
    });
  }
  template <std::size_t, std::size_t... Is, typename U, typename... Args2>
  static auto _apply(const std::shared_ptr<std::tuple<Args2...>>& ptr,
                     detail::index_sequence<Is...>, U&& fn)
      -> promise<decltype(fn(std::declval<Args2>()...))> {
    return resolve(_call(std::forward<U>(fn),
                         detail::make_index_sequence<sizeof...(Args2)>(),
                         std::move(*ptr)));
  }
  template <typename U, std::size_t... Is, typename... Args>
  static auto _call(U&& fn, detail::index_sequence<Is...>,
                    std::tuple<Args...>&& args)
      -> decltype(fn(std::declval<Args>()...)) {
    return fn(std::get<Is>(std::move(args))...);
  }

 public:
  template <typename... Args, typename U, typename... Args2, std::size_t... Is>
  static auto apply(const std::shared_ptr<std::tuple<Args2...>>& ptr, U&& fn,
                    std::tuple<Args...>&& args, detail::index_sequence<Is...>)
      -> promise<decltype(fn(std::declval<Args2>()...))> {
    return _apply<0>(ptr, detail::make_index_sequence<sizeof...(Args) - 1>(),
                     std::forward<U>(fn), std::get<Is>(std::move(args))...);
  }
};
// value operator+
template <typename T, typename T2,
          typename V2 = typename detail::extract_from<
              typename std::decay<T2>::type, value>::type>
auto operator+(const value<T>& v, T2&& v2)
    -> value<decltype(std::declval<T>() + std::declval<V2>())> {
  return v.get([](const T& v, const V2& v2) { return v + v2; },
               std::forward<T2>(v2));
}
template <typename T, typename T2,
          typename V = typename detail::extract_from<
              typename std::decay<T>::type, value>::type,
          typename = typename std::enable_if<!detail::is_value<T>::value>::type>
auto operator+(T&& v, const value<T2>& v2)
    -> value<decltype(std::declval<V>() + std::declval<T2>())> {
  return v2.get([](const T2& v2, const V& v) { return v + v2; },
                std::forward<T>(v));
}
// var operator+
template <typename T, typename T2,
          typename V2 = typename detail::extract_from<
              typename std::decay<T2>::type, value>::type>
auto operator+(const var<T>& v, T2&& v2)
    -> value<decltype(std::declval<T>() + std::declval<V2>())> {
  return v.get([](T* v, const V2& v2) { return (*v) + v2; },
               std::forward<T2>(v2));
}
template <typename T, typename T2,
          typename V = typename detail::extract_from<
              typename std::decay<T>::type, value>::type,
          typename = typename std::enable_if<!detail::is_value<T>::value>::type>
auto operator+(T&& v, const var<T2>& v2)
    -> value<decltype(std::declval<V>() + std::declval<T2>())> {
  return v2.get([](T2* v2, const V& v) { return v + (*v2); },
                std::forward<T>(v));
}
template <typename T, typename T2>
auto operator+(const var<T>& v, const var<T2>& v2)
    -> value<decltype(std::declval<T>() + std::declval<T2>())> {
  return v.get([](T* v, T2* v2) { return (*v) + (*v2); }, v2);
}
// value operator==
template <typename T, typename T2,
          typename V2 = typename detail::extract_from<
              typename std::decay<T2>::type, value>::type>
auto operator==(const value<T>& v, T2&& v2)
    -> value<decltype(std::declval<T>() == std::declval<V2>())> {
  return v.get([](const T& v, const V2& v2) { return v == v2; },
               std::forward<T2>(v2));
}
template <typename T, typename T2,
          typename V = typename detail::extract_from<
              typename std::decay<T>::type, value>::type,
          typename = typename std::enable_if<!detail::is_value<T>::value>::type>
auto operator==(T&& v, const value<T2>& v2)
    -> value<decltype(std::declval<V>() == std::declval<T2>())> {
  return v2.get([](const T2& v2, const V& v) { return v == v2; },
                std::forward<T>(v));
}
// var operator==
template <typename T, typename T2,
          typename V2 = typename detail::extract_from<
              typename std::decay<T2>::type, value>::type>
auto operator==(const var<T>& v, T2&& v2)
    -> value<decltype(std::declval<T>() == std::declval<V2>())> {
  return v.get([](T* v, const V2& v2) { return (*v) == v2; },
               std::forward<T2>(v2));
}
template <typename T, typename T2,
          typename V = typename detail::extract_from<
              typename std::decay<T>::type, value>::type,
          typename = typename std::enable_if<!detail::is_value<T>::value>::type>
auto operator==(T&& v, const var<T2>& v2)
    -> value<decltype(std::declval<V>() == std::declval<T2>())> {
  return v2.get([](T2* v2, const V& v) { return v == (*v2); },
                std::forward<T>(v));
}
template <typename T, typename T2>
auto operator==(const var<T>& v, const var<T2>& v2)
    -> value<decltype(std::declval<T>() + std::declval<T2>())> {
  return v.get([](T* v, T2* v2) { return (*v) == (*v2); }, v2);
}
template <typename T>
struct value {
  template <
      typename U,
      typename = typename std::enable_if<
          (!std::is_same<typename std::decay<U>::type, value<T>>::value) &&
          (!std::is_constructible<typename std::decay<T>::type, U>::value)>::
          type>
  value(U&& fn)
      : ptr(std::make_shared<detail::_value<T>>(std::forward<U>(fn))){};
  value(const value<T>& v) : ptr(v.ptr){};
  value(value<T>&& v) : ptr(std::move(v.ptr)){};
  template <typename... Args,
            typename = typename std::enable_if<!std::is_same<
                typename std::decay<Args...>::type, value<T>>::value>::type>
  value(Args&&... args) {
    auto _v = detail::capture(T(std::forward<Args>(args)...));
    ptr = std::make_shared<detail::_value<T>>(
        [_v]() mutable { return resolve<T>(std::move(_v.borrow())); });
  }
  template <typename U, typename... Args,
            typename Ret = decltype(std::declval<U>()(
                std::declval<T>(),
                std::declval<typename detail::extract_from<
                    typename std::decay<Args>::type, value>::type>()...))>
  auto get(U&& fn, Args&&... args) const -> value<Ret> {
    auto _fn = detail::capture(std::forward<U>(fn));
    auto _args = detail::capture(std::make_tuple(std::forward<Args>(args)...));
    auto val = *this;
    return value<Ret>([val, _fn, _args]() mutable {
      return _value_get_impl::apply(
          std::make_shared<std::tuple<
              T, typename detail::extract_from<typename std::decay<Args>::type,
                                               value>::type...>>(),
          std::move(_fn.borrow()),
          std::tuple_cat(std::forward_as_tuple(val), std::move(_args.borrow())),
          detail::make_index_sequence<sizeof...(Args) + 1>());
    });
  }

  template <typename Ctx>
  promise<void> apply(context<Ctx>&) const {
    return apply();
  }
  promise<void> apply() const { return ptr->apply(); }
  promise<T> get() const noexcept { return ptr->get(); }

  std::shared_ptr<detail::_value<T>> ptr;
};
template <typename T>
struct value<T*> {
  template <
      typename U,
      typename = typename std::enable_if<
          (!std::is_same<typename std::decay<U>::type, value<T*>>::value) &&
          (!std::is_constructible<typename std::decay<T>::type*, U>::value)>::
          type>
  value(U&& fn)
      : ptr(std::make_shared<detail::_value<T*>>(std::forward<U>(fn))){};
  value(const value<T*>& v) : ptr(v.ptr){};
  value(value<T*>&& v) : ptr(std::move(v.ptr)){};
  value(T* val) {
    ptr = std::make_shared<detail::_value<T*>>(
        [val]() mutable { return resolve<T*>(std::move(val)); });
  }
  template <typename T2, typename V2 = typename detail::extract_from<
                             typename std::decay<T2>::type, value>::type>
  auto operator=(T2&& v) -> value<T*> {
    return get([](T* v, const V2& v2) { return &((*v) = v2); },
               std::forward<T2>(v));
  }
  template <typename U, typename... Args,
            typename Ret = decltype(std::declval<U>()(
                std::declval<T*>(),
                std::declval<typename detail::extract_from<
                    typename std::decay<Args>::type, value>::type>()...))>
  auto get(U&& fn, Args&&... args) const -> value<Ret> {
    auto _fn = detail::capture(std::forward<U>(fn));
    auto _args = detail::capture(std::make_tuple(std::forward<Args>(args)...));
    auto val = *this;
    return value<Ret>([val, _fn, _args]() mutable {
      return _value_get_impl::apply(
          std::make_shared<std::tuple<
              T*, typename detail::extract_from<typename std::decay<Args>::type,
                                                value>::type...>>(),
          std::move(_fn.borrow()),
          std::tuple_cat(std::forward_as_tuple(val), std::move(_args.borrow())),
          detail::make_index_sequence<sizeof...(Args) + 1>());
    });
  }

  template <typename Ctx>
  promise<void> apply(context<Ctx>&) const {
    return apply();
  }
  promise<void> apply() const { return ptr->apply(); }
  promise<T*> get() const noexcept { return ptr->get(); }

  std::shared_ptr<detail::_value<T*>> ptr;
};
template <>
struct value<void> {
  template <typename U,
            typename = typename std::enable_if<!std::is_same<
                typename std::decay<U>::type, value<void>>::value>::type>
  value(U&& fn)
      : ptr(std::make_shared<detail::_value<void>>(std::forward<U>(fn))){};
  value(const value<void>& v) : ptr(v.ptr){};
  value(value<void>&& v) : ptr(std::move(v.ptr)){};
  value() {
    ptr = std::make_shared<detail::_value<void>>(
        []() mutable { return resolve(); });
  }

  template <typename U, typename... Args,
            typename Ret = decltype(std::declval<U>()(
                std::declval<typename detail::extract_from<
                    typename std::decay<Args>::type, value>::type>()...))>
  auto get(U&& fn, Args&&... args) const -> value<Ret> {
    auto _fn = detail::capture(std::forward<U>(fn));
    auto _args = detail::capture(std::make_tuple(std::forward<Args>(args)...));
    return value<Ret>([_fn, _args]() mutable {
      return _value_get_impl::apply(
          std::make_shared<std::tuple<typename detail::extract_from<
              typename std::decay<Args>::type, value>::type...>>(),
          std::move(_fn.borrow()), std::move(_args.borrow()),
          detail::make_index_sequence<sizeof...(Args) + 1>());
    });
  }

  template <typename Ctx>
  promise<void> apply(context<Ctx>&) const {
    return apply();
  }
  promise<void> apply() const { return ptr->apply(); }
  promise<void> get() const noexcept { return ptr->get(); }

  std::shared_ptr<detail::_value<void>> ptr;
};
};  // namespace stmt
template <typename T>
struct context;
namespace detail {
template <typename T>
struct basic_context : public std::enable_shared_from_this<context<T>> {
  template <typename U>
  static stmt::value<U> await(const stmt::value<promise<U>>& v) {
    return stmt::value<U>([v]() {
      return v.apply().then([v]() {
        return v.get().then([](const promise<U>& v) { return v; });
      });
    });
  }
  static stmt::expr<T> error(const stmt::value<std::exception_ptr>& v) {
    return stmt::expr<T>([v](context<T>& ctx) {
      return v.apply()
          .then([v]() { return v.get(); })
          .then([&ctx](const std::exception_ptr& v) { ctx.reject(v); });
    });
  }
  template <typename U>
  static stmt::expr<T> cond(const stmt::value<bool>& v, U&& if_true) {
    auto _if_true = detail::capture(std::forward<U>(if_true));
    return stmt::expr<T>([v, _if_true](context<T>& ctx) mutable {
      return v.apply()
          .then([v]() { return v.get(); })
          .then([&ctx, _if_true](bool v) mutable {
            if (v) {
              auto tmp_ctx = context<T>::_create(&ctx);
              _if_true.borrow()(*tmp_ctx);
            }
            return resolve();
          });
    });
  }
  template <typename U, typename U2>
  static stmt::expr<T> cond(const stmt::value<bool>& v, U&& if_true,
                            U2&& if_false) {
    auto _if_true = detail::capture(std::forward<U>(if_true));
    auto _if_false = detail::capture(std::forward<U2>(if_false));
    return stmt::expr<T>([v, _if_true, _if_false](context<T>& ctx) mutable {
      return v.apply()
          .then([v]() { return v.get(); })
          .then([&ctx, _if_true, _if_false](bool v) mutable {
            auto tmp_ctx = context<T>::_create(&ctx);
            if (v) {
              _if_true.borrow()(*tmp_ctx);
            } else {
              _if_false.borrow()(*tmp_ctx);
            }
            return resolve();
          });
    });
  }
  template <typename U>
  stmt::var<U> create(const std::string& name, U&& v) {
    using Decay = typename std::decay<U>::type;
    return stmt::var<U>(
        (Decay*)local
            .insert({name, std::unique_ptr<void, void (*)(void*)>(
                               new Decay(std::forward<U>(v)),
                               [](void* ptr) { ((Decay*)ptr)->~Decay(); })})
            .first->second.get());
  }
  template <typename U>
  stmt::var<U> create(std::string&& name, U&& v) {
    using Decay = typename std::decay<U>::type;
    return stmt::var<U>(
        (Decay*)local
            .insert({std::move(name),
                     std::unique_ptr<void, void (*)(void*)>(
                         new Decay(std::forward<U>(v)),
                         [](void* ptr) { ((Decay*)ptr)->~Decay(); })})
            .first->second.get());
  }

 protected:
  std::unordered_map<std::string, std::unique_ptr<void, void (*)(void*)>> local;
};
};  // namespace detail
template <typename T>
struct context : public detail::basic_context<T> {
  template <typename U>
  context& operator<<(U&& v) {
    auto _v = detail::capture(std::forward<U>(v));
    auto ptr = this->shared_from_this();
    if (parent) {
      parent->chain = parent->chain.then(
          [ptr, _v]() mutable { return _v.borrow().apply(*ptr); });
    } else {
      chain =
          chain.then([ptr, _v]() mutable { return _v.borrow().apply(*ptr); });
    }
    return *this;
  }
  static stmt::expr<T> ret(const stmt::value<T>& v) {
    return stmt::expr<T>([v](context<T>& ctx) {
      return v.apply().then([v]() { return v.get(); }).then([&ctx](const T& v) {
        ctx.resolve(v);
      });
    });
  }
  template <typename U>
  stmt::value<U> get(const std::string& name) const {
    auto it = this->local.find(name);
    if (it != this->local.cend()) {
      return stmt::var<U>((U*)it->second.get());
    }
    if (parent) {
      return parent->template get<U>(name);
    }
    throw std::invalid_argument("context<void>::get(" + name + ")");
  }

 private:
  template <typename>
  friend struct detail::basic_context;
  template <typename U>
  friend promise<T> async(U&&);
  static std::shared_ptr<context<T>> create(context<T>* parent) {
    return std::shared_ptr<context<T>>(new context<T>(parent));
  }
  promise<T> get_result() const {
    if (parent) return parent->result;
    return result;
  }
  void resolve(const T& v) const {
    if (parent)
      parent->result.resolve(v);
    else
      result.resolve(v);
  }
  void resolve(T&& v) const {
    if (parent)
      parent->result.resolve(std::move(v));
    else
      result.resolve(std::move(v));
  }
  void reject(const std::exception_ptr& v) const {
    if (parent)
      parent->result.reject(v);
    else
      result.reject(v);
  }
  void reject(std::exception_ptr&& v) const {
    if (parent)
      parent->result.reject(std::move(v));
    else
      result.reject(std::move(v));
  }
  context(context<T>* parent) : parent(parent) { chain.resolve(); }
  promise<T> result;
  promise<void> chain;
  context<T>* parent;
};
template <>
struct context<void> : public detail::basic_context<void> {
  template <typename U>
  context& operator<<(U&& v) {
    auto _v = detail::capture(std::forward<U>(v));
    auto ptr = this->shared_from_this();
    if (parent) {
      parent->chain = parent->chain.then(
          [ptr, _v]() mutable { return _v.borrow().apply(*ptr); });
    } else {
      chain =
          chain.then([ptr, _v]() mutable { return _v.borrow().apply(*ptr); });
    }
    return *this;
  }
  static stmt::expr<void> ret() {
    return stmt::expr<void>([](context<void>& ctx) {
      ctx.resolve();
      return awacorn::resolve();
    });
  }
  template <typename U>
  stmt::value<U> get(const std::string& name) const {
    auto it = this->local.find(name);
    if (it != this->local.cend()) {
      return stmt::var<U>((U*)it->second.get());
    }
    if (parent) {
      return parent->template get<U>(name);
    }
    throw std::invalid_argument("context<void>::get(" + name + ")");
  }

 private:
  template <typename>
  friend struct detail::basic_context;
  template <typename T, typename U>
  friend promise<T> async(U&&);
  static std::shared_ptr<context<void>> _create(context<void>* parent) {
    return std::shared_ptr<context<void>>(new context<void>(parent));
  }
  promise<void> get_result() const {
    if (parent) return parent->result;
    return result;
  }
  void resolve() const {
    if (parent)
      parent->result.resolve();
    else
      result.resolve();
  }
  void reject(const std::exception_ptr& v) const {
    if (parent)
      parent->result.reject(v);
    else
      result.reject(v);
  }
  void reject(std::exception_ptr&& v) const {
    if (parent)
      parent->result.reject(std::move(v));
    else
      result.reject(std::move(v));
  }
  context(context<void>* parent) : parent(parent) { chain.resolve(); };
  promise<void> result;
  promise<void> chain;
  context<void>* parent;
};
/**
 * @brief 开始异步函数。
 *
 * @tparam T 函数的返回类型。
 * @tparam U 实际逻辑函数的类型。
 * @param fn 实际逻辑函数。
 * @return promise<T> 返回的 promise。
 */
template <typename T, typename U>
promise<T> async(U&& fn) {
  auto ctx = context<T>::_create(nullptr);
  fn(*ctx);
  return ctx->get_result();
}
}  // namespace awacorn
#endif
#endif