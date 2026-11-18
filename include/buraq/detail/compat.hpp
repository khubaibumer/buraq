/// @file
/// @brief C++20 fallbacks for C++23 library features.
///
/// Provides buraq::expected / buraq::unexpected (aliases of std::expected /
/// std::unexpected when available, otherwise a minimal std::variant-based
/// fallback) and defines BURAQ_HAS_STD_GENERATOR, which is 1 (and exposes
/// buraq::generator as std::generator) when the standard library provides
/// `<generator>`, else 0.
#pragma once

#include <version>
#include <utility>
#include <variant>
#include <functional>

// 1. expected<T, E>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
#include <expected>
namespace buraq {
/// Alias of std::expected (C++23 standard library available).
using std::expected;
/// Alias of std::unexpected (C++23 standard library available).
using std::unexpected;
}
#else
namespace buraq {
/// Minimal fallback for std::unexpected: wraps an error value.
/// @tparam E Error type.
template <typename E>
struct unexpected {
  /// The wrapped error value.
  E error;
  /// Wraps @p e as an unexpected error.
  /// @param e Error value (moved in).
  explicit constexpr unexpected(E e) : error(std::move(e)) {}
};

/// Minimal fallback for std::expected holding either a value or an error.
///
/// Backed by `std::variant<T, unexpected<E>>`. Accessing the wrong alternative
/// throws std::bad_variant_access (via std::get).
/// @tparam T Value type.
/// @tparam E Error type.
template <typename T, typename E>
class expected {
  std::variant<T, unexpected<E>> storage_;
public:
  /// Constructs an expected holding a value.
  /// @param val Value (moved in).
  constexpr expected(T val) : storage_(std::move(val)) {}
  /// Constructs an expected holding an error.
  /// @param err Error wrapper (moved in).
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  /// Checks whether a value is held.
  /// @return true if a value is held, false if an error is held.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }
  /// Checks whether a value is held.
  /// @return has_value().
  constexpr explicit operator bool() const noexcept { return has_value(); }

  /// Returns the held value.
  /// @return Reference to the held value.
  constexpr T& value() & { return std::get<T>(storage_); }
  /// Returns the held value (const).
  /// @return Const reference to the held value.
  constexpr const T& value() const& { return std::get<T>(storage_); }
  /// Returns the held value (rvalue).
  /// @return Rvalue reference to the held value.
  constexpr T&& value() && { return std::get<T>(std::move(storage_)); }

  /// Dereferences to the held value.
  /// @return value().
  constexpr T& operator*() & { return value(); }
  /// Dereferences to the held value (const).
  /// @return value().
  constexpr const T& operator*() const& { return value(); }
  /// Dereferences to the held value (rvalue).
  /// @return The held value, moved.
  constexpr T&& operator*() && { return std::move(value()); }

  /// Member access to the held value.
  /// @return Pointer to the held value.
  constexpr T* operator->() { return &value(); }
  /// Member access to the held value (const).
  /// @return Const pointer to the held value.
  constexpr const T* operator->() const { return &value(); }

  /// Returns the held error.
  /// @return Reference to the held error.
  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  /// Returns the held error (const).
  /// @return Const reference to the held error.
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  /// Chains an operation on the held value.
  /// @tparam F Callable taking `T&` and returning an expected with error type E.
  /// @param f Invoked with value() if a value is held.
  /// @return The result of @p f, or the held error wrapped in the result type of @p f.
  template <typename F>
  constexpr auto and_then(F&& f) & {
    using Ret = std::invoke_result_t<F, T&>;
    if (has_value()) return std::invoke(std::forward<F>(f), value());
    return Ret(unexpected<E>(error()));
  }
};

/// Fallback expected specialization for operations that yield no value.
/// @tparam E Error type.
template <typename E>
class expected<void, E> {
  std::variant<std::monostate, unexpected<E>> storage_;
public:
  /// Constructs a successful (value-holding) expected.
  constexpr expected() noexcept : storage_(std::monostate{}) {}
  /// Constructs an expected holding an error.
  /// @param err Error wrapper (moved in).
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  /// Checks whether the operation succeeded.
  /// @return true if no error is held.
  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<std::monostate>(storage_);
  }
  /// Checks whether the operation succeeded.
  /// @return has_value().
  constexpr explicit operator bool() const noexcept { return has_value(); }

  /// No-op value access (there is no value).
  constexpr void value() const noexcept {}
  /// No-op dereference (there is no value).
  constexpr void operator*() const noexcept {}

  /// Returns the held error.
  /// @return Reference to the held error.
  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  /// Returns the held error (const).
  /// @return Const reference to the held error.
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  /// Chains an operation on success.
  /// @tparam F Nullary callable returning an expected with error type E.
  /// @param f Invoked if no error is held.
  /// @return The result of @p f, or the held error wrapped in the result type of @p f.
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
/// Defined to 1 when std::generator is available (buraq::generator is then provided), else 0.
#define BURAQ_HAS_STD_GENERATOR 1
namespace buraq {
/// Alias of std::generator, available only when BURAQ_HAS_STD_GENERATOR is 1.
using std::generator;
}
#else
/// Defined to 1 when std::generator is available (buraq::generator is then provided), else 0.
#define BURAQ_HAS_STD_GENERATOR 0
#endif