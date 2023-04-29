#ifndef _AWACORN_ASYNC
#define _AWACORN_ASYNC
#if __cplusplus >= 201101L
/**
 * Project Awacorn 基于 MIT 协议开源。
 * Copyright(c) 凌 2023.
 */
#if !defined(AWACORN_USE_BOOST) && !defined(AWACORN_USE_UCONTEXT)
#if __has_include(<boost/context/continuation.hpp>)
#define AWACORN_USE_BOOST
#elif __has_include(<ucontext.h>)
#define AWACORN_USE_UCONTEXT
#else
#error Neither <boost/context/continuation.hpp> nor <ucontext.h> is found.
#endif
#endif
#if defined(AWACORN_USE_BOOST)
#include <boost/context/continuation.hpp>
#elif defined(AWACORN_USE_UCONTEXT)
#ifdef __APPLE__
#define _XOPEN_SOURCE
#endif
#include <ucontext.h>
#endif

#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <vector>

#include "detail/function.hpp"
#include "promise.hpp"

namespace awacorn {
namespace detail {
/**
 * @brief 生成器的状态。
 */
enum class _async_state_t {
  Pending = 0,   // 尚未运行
  Active = 1,    // 运行中
  Returned = 2,  // 已返回
  Awaiting = 3,  // await 中
  Throwed = 4    // 抛出错误
};
template <typename Fn>
struct _basic_async_fn;
template <typename RetType>
struct _async_fn;
};  // namespace detail
/**
 * @brief 生成器上下文基类。
 */
struct context {
#if defined(AWACORN_USE_BOOST)
  context(void (*fn)(void*), void* arg, size_t stack_size = 0)
      : _status(detail::_async_state_t::Pending),
        _ctx(boost::context::callcc(
            std::allocator_arg,
            boost::context::fixedsize_stack(
                stack_size ? stack_size
                           : boost::context::stack_traits::default_size()),
            [this, fn, arg](boost::context::continuation&& ctx) {
              _ctx = std::move(ctx);
              _ctx = _ctx.resume();
              fn(arg);
              return std::move(_ctx);
            })) {}
#elif defined(AWACORN_USE_UCONTEXT)
  context(void (*fn)(void*), void* arg, size_t stack_size = 0)
      : _status(detail::_async_state_t::Pending),
        _stack(nullptr, [](char* ptr) {
          if (ptr) delete[] ptr;
        }) {
    getcontext(&_ctx);
    if (!stack_size) stack_size = 128 * 1024;  // default stack size
    _stack.reset(new char[stack_size]);
    _ctx.uc_stack.ss_sp = _stack.get();
    _ctx.uc_stack.ss_size = stack_size;
    _ctx.uc_stack.ss_flags = 0;
    _ctx.uc_link = nullptr;
    makecontext(&_ctx, (void (*)(void))fn, 1, arg);
  }
#else
#error Please define "AWACORN_USE_UCONTEXT" or "AWACORN_USE_BOOST".
#endif
  context(const context&) = delete;
  /**
   * @brief 等待 promise 完成并返回 promise 的结果值。
   *
   * @tparam T promise 的结果类型。
   * @param value promise 本身。
   * @return T promise 的结果。
   */
  template <typename T>
  T operator>>(const promise<T>& value) {
    if (_status != detail::_async_state_t::Active)
      throw std::bad_function_call();
    _status = detail::_async_state_t::Awaiting;
    _result = value.then([](const T& v) { return any(v); });
    resume();
    if (_failbit) {
      _failbit = false;
      std::rethrow_exception(any_cast<std::exception_ptr>(_result));
    }
    return any_cast<T>(_result);
  }
  void operator>>(const promise<void>& value) {
    if (_status != detail::_async_state_t::Active)
      throw std::bad_function_call();
    _status = detail::_async_state_t::Awaiting;
    _result = value.then([]() { return any(); });
    resume();
    if (_failbit) {
      _failbit = false;
      std::rethrow_exception(any_cast<std::exception_ptr>(_result));
    }
  }

 private:
  void resume() {
#if defined(AWACORN_USE_BOOST)
    _ctx = _ctx.resume();
#elif defined(AWACORN_USE_UCONTEXT)
    ucontext_t orig = _ctx;
    swapcontext(&_ctx, &orig);
#else
#error Please define "AWACORN_USE_UCONTEXT" or "AWACORN_USE_BOOST".
#endif
  }
  detail::_async_state_t _status;
  bool _failbit;
  any _result;
#if defined(AWACORN_USE_BOOST)
  boost::context::continuation _ctx;
#elif defined(AWACORN_USE_UCONTEXT)
  ucontext_t _ctx;
  std::unique_ptr<char, void (*)(char*)> _stack;
#else
#error Please define "AWACORN_USE_UCONTEXT" or "AWACORN_USE_BOOST".
#endif
  template <typename T>
  friend struct detail::_async_fn;
  template <typename T>
  friend struct detail::_basic_async_fn;
};
namespace detail {
template <typename Fn>
struct _basic_async_fn {
 protected:
  context ctx;
  function<Fn> fn;
  template <typename U>
  _basic_async_fn(U&& fn, void (*run_fn)(void*), void* args,
                  size_t stack_size = 0)
      : ctx(run_fn, args, stack_size), fn(std::forward<U>(fn)) {}
  _basic_async_fn(const _basic_async_fn& v) = delete;
};
template <typename RetType>
struct _async_fn : public _basic_async_fn<RetType(context&)>,
                   public std::enable_shared_from_this<_async_fn<RetType>> {
  explicit _async_fn(const _async_fn& v) = delete;
  promise<RetType> next() {
    if (this->ctx._status == _async_state_t::Pending) {
      this->ctx._status = _async_state_t::Active;
      this->ctx.resume();
      if (this->ctx._status == _async_state_t::Awaiting) {
        std::shared_ptr<_async_fn> ref = this->shared_from_this();
        promise<RetType> pm;
        promise<any> tmp = any_cast<promise<any>>(this->ctx._result);
        tmp.then([ref, pm](std::exception_ptr res) {
             ref->ctx._result = res;
             ref->_await_next()
                 .then([pm](const RetType& res) { pm.resolve(res); })
                 .error(
                     [pm](const std::exception_ptr& err) { pm.reject(err); });
           })
            .error([ref, pm](const std::exception_ptr& err) {
              ref->ctx._result = err;
              ref->ctx._failbit = true;
              ref->_await_next()
                  .then([pm](const RetType& res) { pm.resolve(res); })
                  .error(
                      [pm](const std::exception_ptr& err) { pm.reject(err); });
            });
        return pm;
      } else if (this->ctx._status == _async_state_t::Returned) {
        return resolve(any_cast<RetType>(this->ctx._result));
      }
      return reject<RetType>(any_cast<std::exception_ptr>(this->ctx._result));
    } else if (this->ctx._status == _async_state_t::Returned) {
      return resolve(any_cast<RetType>(this->ctx._result));
    }
    return reject<RetType>(any_cast<std::exception_ptr>(this->ctx._result));
  }
  template <typename... Args>
  static inline std::shared_ptr<_async_fn> create(Args&&... args) {
    return std::shared_ptr<_async_fn>(
        new _async_fn(std::forward<Args>(args)...));
  }

 private:
  inline promise<RetType> _await_next() {
    this->ctx._status = _async_state_t::Pending;
    return this->next();
  }
  template <typename U>
  explicit _async_fn(U&& fn, size_t stack_size = 0)
      : _basic_async_fn<RetType(context&)>(
            std::forward<U>(fn), (void (*)(void*))run_fn, this, stack_size) {}
  static void run_fn(_async_fn* self) {
    try {
      self->ctx._result = self->fn(self->ctx);
      self->ctx._status = _async_state_t::Returned;
    } catch (...) {
      self->ctx._result = std::current_exception();
      self->ctx._status = _async_state_t::Throwed;
    }
    self->ctx.resume();
  }
};
template <>
struct _async_fn<void> : public _basic_async_fn<void(context&)>,
                         public std::enable_shared_from_this<_async_fn<void>> {
  explicit _async_fn(const _async_fn& v) = delete;
  promise<void> next() {
    if (this->ctx._status == _async_state_t::Pending) {
      this->ctx._status = _async_state_t::Active;
      this->ctx.resume();
      if (this->ctx._status == _async_state_t::Awaiting) {
        std::shared_ptr<_async_fn> ref = this->shared_from_this();
        promise<void> pm;
        promise<any> tmp = any_cast<promise<any>>(this->ctx._result);
        tmp.then([ref, pm](const any& res) {
             ref->ctx._result = res;
             ref->_await_next()
                 .then([pm]() { pm.resolve(); })
                 .error(
                     [pm](const std::exception_ptr& err) { pm.reject(err); });
           })
            .error([ref, pm](const std::exception_ptr& err) {
              ref->ctx._result = err;
              ref->ctx._failbit = true;
              ref->_await_next()
                  .then([pm]() { pm.resolve(); })
                  .error(
                      [pm](const std::exception_ptr& err) { pm.reject(err); });
            });
        return pm;
      } else if (this->ctx._status == _async_state_t::Returned) {
        return resolve();
      }
      return reject<void>(any_cast<std::exception_ptr>(this->ctx._result));
    } else if (this->ctx._status == _async_state_t::Returned) {
      return resolve();
    }
    return reject<void>(any_cast<std::exception_ptr>(this->ctx._result));
  }
  template <typename... Args>
  static inline std::shared_ptr<_async_fn> create(Args&&... args) {
    return std::shared_ptr<_async_fn>(
        new _async_fn(std::forward<Args>(args)...));
  }

 private:
  inline promise<void> _await_next() {
    this->ctx._status = _async_state_t::Pending;
    return this->next();
  }
  template <typename U>
  explicit _async_fn(U&& fn, size_t stack_size = 0)
      : _basic_async_fn<void(context&)>(
            std::forward<U>(fn), (void (*)(void*))run_fn, this, stack_size) {}
  static void run_fn(_async_fn* self) {
    try {
      self->fn(self->ctx);
      self->ctx._status = _async_state_t::Returned;
    } catch (...) {
      self->ctx._result = std::current_exception();
      self->ctx._status = _async_state_t::Throwed;
    }
    self->ctx.resume();
  }
};
};  // namespace detail
/**
 * @brief 进入异步函数上下文。
 *
 * @tparam U 函数类型。
 * @param fn 函数。
 * @param stack_size 可选，栈的大小(如果可用)。
 * @return promise<decltype(fn(std::declval<context&>()))> 用于取得函数返回值的
 * promise 对象。
 */
template <typename U>
auto async(U&& fn, size_t stack_size = 0)
    -> promise<decltype(fn(std::declval<context&>()))> {
  return detail::_async_fn<decltype(fn(std::declval<context&>()))>::create(
             std::forward<U>(fn), stack_size)
      ->next();
}
};  // namespace awacorn
#endif
#endif
