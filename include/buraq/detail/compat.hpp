#pragma once

#include <version>
#include <utility>
#include <variant>
#include <functional>

// 1. expected<T, E>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
#include <expected>
namespace buraq {
using std::expected;
using std::unexpected;
}
#else
namespace buraq {
template <typename E>
struct unexpected {
  E error;
  explicit constexpr unexpected(E e) : error(std::move(e)) {}
};

template <typename T, typename E>
class expected {
  std::variant<T, unexpected<E>> storage_;
public:
  constexpr expected(T val) : storage_(std::move(val)) {}
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr T& value() & { return std::get<T>(storage_); }
  constexpr const T& value() const& { return std::get<T>(storage_); }
  constexpr T&& value() && { return std::get<T>(std::move(storage_)); }

  constexpr T& operator*() & { return value(); }
  constexpr const T& operator*() const& { return value(); }
  constexpr T&& operator*() && { return std::move(value()); }

  constexpr T* operator->() { return &value(); }
  constexpr const T* operator->() const { return &value(); }

  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  template <typename F>
  constexpr auto and_then(F&& f) & {
    using Ret = std::invoke_result_t<F, T&>;
    if (has_value()) return std::invoke(std::forward<F>(f), value());
    return Ret(unexpected<E>(error()));
  }
};

template <typename E>
class expected<void, E> {
  std::variant<std::monostate, unexpected<E>> storage_;
public:
  constexpr expected() noexcept : storage_(std::monostate{}) {}
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<std::monostate>(storage_);
  }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr void value() const noexcept {}
  constexpr void operator*() const noexcept {}

  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  template <typename F>
  constexpr auto and_then(F&& f) & {
    using Ret = std::invoke_result_t<F>;
    if (has_value()) return std::invoke(std::forward<F>(f));
    return Ret(unexpected<E>(error()));
  }
};
}
#endif

// 2. generator<T> opt-in sugar (GCC 14+ / MSVC with -std=c++23)
#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
#include <generator>
#define BURAQ_HAS_STD_GENERATOR 1
namespace buraq {
using std::generator;
}
#else
#define BURAQ_HAS_STD_GENERATOR 0
#endif