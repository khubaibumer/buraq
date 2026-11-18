// ============================================================================
// buraq v0.1.0 - Agent Client Protocol (ACP) v1 SDK for C++20/23
// https://github.com/khubaibumer/buraq
//
// Single-header distribution: buraq/acp.hpp
// Zero third-party dependencies; codecs ship as separate headers.
//
// GENERATED FILE - DO NOT EDIT. Regenerate with: python3 scripts/amalgamate.py
// SPDX-License-Identifier: Apache-2.0
// ============================================================================
#pragma once

// ---- begin buraq/detail/compat.hpp ----
/// @file
/// @brief C++20 fallbacks for C++23 library features.
///
/// Provides buraq::expected / buraq::unexpected (aliases of std::expected /
/// std::unexpected when available, otherwise a minimal std::variant-based
/// fallback) and defines BURAQ_HAS_STD_GENERATOR, which is 1 (and exposes
/// buraq::generator as std::generator) when the standard library provides
/// `<generator>`, else 0.

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
// ---- end buraq/detail/compat.hpp ----

// ---- begin buraq/protocol/jsonrpc.hpp ----
/// @file
/// @brief JSON-RPC 2.0 envelope types and the pluggable JSON codec contract.
///
/// Defines RequestId, RawJson, the error_code constants, RpcError and its
/// factories, RawEnvelope, and the JsonCodec / JsonSerializable concepts.
/// A codec type must provide these static operations:
/// - `parse_envelope(std::span<const std::byte>)` -> `expected<RawEnvelope, RpcError>`
/// - `encode_error(const RequestId&, const RpcError&)` -> `std::string`
/// - `decode<T>(std::string_view)` -> `expected<T, RpcError>`
/// - `encode_result<T>(const RequestId&, const T&)` -> `std::string`
/// - `encode_request<T>(const RequestId&, std::string_view method, const T&)` -> `std::string`
/// - `encode_notification<T>(std::string_view method, const T&)` -> `std::string`

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace buraq {

/// JSON-RPC 2.0 request id: an integer or a string.
using RequestId = std::variant<std::int64_t, std::string>;

// Arbitrary, codec-neutral JSON value kept as serialized text (e.g. `rawInput`, elicitation schemas,
// extension-method payloads). An empty string encodes as JSON `null`.
/// Arbitrary, codec-neutral JSON value kept as serialized text.
///
/// An empty string encodes as JSON `null`.
struct RawJson {
    /// Serialized JSON text (empty means `null`).
    std::string json{};
    /// Lexicographic comparison of the serialized text.
    auto operator<=>(const RawJson&) const = default;
};

namespace detail {

// Serialize `text` as a JSON string literal (RFC 8259 escaping)
/// Serializes @p text as a JSON string literal (RFC 8259 escaping).
/// @param text Raw text to quote.
/// @return The quoted and escaped JSON string, including surrounding quotes.
inline std::string json_quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
    return out;
}

} // namespace detail

// JSON-RPC 2.0 and ACP v1 error codes
namespace error_code {
/// Invalid JSON was received (-32700).
inline constexpr int parse_error = -32700;
/// The JSON sent is not a valid request object (-32600).
inline constexpr int invalid_request = -32600;
/// The method does not exist or is not available (-32601).
inline constexpr int method_not_found = -32601;
/// Invalid method parameters (-32602).
inline constexpr int invalid_params = -32602;
/// Internal JSON-RPC error (-32603).
inline constexpr int internal_error = -32603;
/// The request was cancelled (-32800).
inline constexpr int request_cancelled = -32800;
/// Authentication is required (ACP, -32000).
inline constexpr int auth_required = -32000;
/// The requested resource was not found (ACP, -32002).
inline constexpr int resource_not_found = -32002;
} // namespace error_code

/// A JSON-RPC 2.0 error object.
struct RpcError {
    /// Numeric error code (defaults to -32603, internal error).
    int code{-32603};
    /// Human-readable error message.
    std::string message;
    /// Optional `data` member, as serialized JSON text.
    std::optional<std::string> data_json{};

    /// Creates a parse error (-32700).
    /// @param msg Error message.
    /// @return The error.
    static RpcError parse_error(std::string_view msg = "Parse error") {
        return {-32700, std::string(msg), std::nullopt};
    }
    /// Creates an invalid request error (-32600).
    /// @param msg Error message.
    /// @return The error.
    static RpcError invalid_request(std::string_view msg = "Invalid Request") {
        return {-32600, std::string(msg), std::nullopt};
    }
    /// Creates a method not found error (-32601) with message "Method not found: <method>".
    /// @param method Name of the unknown method.
    /// @return The error.
    static RpcError method_not_found(std::string_view method) {
        return {-32601, "Method not found: " + std::string(method), std::nullopt};
    }
    /// Creates an invalid params error (-32602).
    /// @param msg Error message.
    /// @return The error.
    static RpcError invalid_params(std::string_view msg = "Invalid params") {
        return {-32602, std::string(msg), std::nullopt};
    }
    /// Creates an internal error (-32603).
    /// @param msg Error message.
    /// @return The error.
    static RpcError internal_error(std::string_view msg = "Internal error") {
        return {-32603, std::string(msg), std::nullopt};
    }
    /// Creates a request cancelled error (-32800) with message "Request cancelled".
    /// @return The error.
    static RpcError request_cancelled() {
        return {-32800, "Request cancelled", std::nullopt};
    }
    /// Creates an authentication required error (-32000).
    /// @param msg Error message.
    /// @return The error.
    static RpcError auth_required(std::string_view msg = "Authentication required") {
        return {-32000, std::string(msg), std::nullopt};
    }
    /// Creates a resource not found error (-32002).
    /// @param msg Error message.
    /// @param data_json Optional `data` member as serialized JSON.
    /// @return The error.
    static RpcError resource_not_found(std::string_view msg = "Resource not found",
                                       std::optional<std::string> data_json = std::nullopt) {
        return {-32002, std::string(msg), std::move(data_json)};
    }
    // -32002 with `data.sessionId`, as recommended for unknown or closed sessions
    /// Creates a resource not found error (-32002) for an unknown or closed session.
    ///
    /// The message is "Session not found: <id>" and `data` is `{"sessionId": <id>}`.
    /// @param session_id The session id.
    /// @return The error.
    static RpcError session_not_found(std::string_view session_id) {
        return resource_not_found("Session not found: " + std::string(session_id),
                                  "{\"sessionId\":" + detail::json_quote(session_id) + "}");
    }
};

// Raw view over a decoded JSON-RPC envelope (params/result kept as raw JSON slices)
/// Raw view over a decoded JSON-RPC envelope.
///
/// Params and result are kept as raw JSON slices (views into the source frame)
/// so payloads can be decoded lazily.
struct RawEnvelope {
    /// Request id, absent for notifications.
    std::optional<RequestId> id{};
    /// Method name, empty for responses.
    std::string_view method{};
    /// Raw JSON of `params`, empty if absent.
    std::string_view params_json{};
    /// Raw JSON of `result`, empty if absent.
    std::string_view result_json{};
    /// Error object of an error response, if any.
    std::optional<RpcError> error{};

    /// Checks whether this is a request (has id and method).
    /// @return true for requests.
    [[nodiscard]] bool is_request() const noexcept {
        return id.has_value() && !method.empty();
    }
    /// Checks whether this is a notification (method without id).
    /// @return true for notifications.
    [[nodiscard]] bool is_notification() const noexcept {
        return !id.has_value() && !method.empty();
    }
    /// Checks whether this is a response (id without method).
    /// @return true for responses.
    [[nodiscard]] bool is_response() const noexcept {
        return id.has_value() && method.empty();
    }
};

// Pluggable zero-overhead JSON codec contract
/// Pluggable zero-overhead JSON codec contract.
///
/// Requires static `Codec::parse_envelope(std::span<const std::byte>)` returning
/// `expected<RawEnvelope, RpcError>` (parses the top-level envelope without decoding
/// the payload) and `Codec::encode_error(const RequestId&, const RpcError&)`
/// returning `std::string` (encodes an error response).
/// @tparam Codec Codec type.
template <typename Codec>
concept JsonCodec = requires(std::span<const std::byte> frame,
                             std::string_view raw_json,
                             const RequestId& id,
                             const RpcError& err) {
    // Parse top-level JSON-RPC 2.0 envelope without eagerly decoding payload
    { Codec::parse_envelope(frame) } -> std::same_as<expected<RawEnvelope, RpcError>>;

    // Encode a JSON-RPC 2.0 error response
    { Codec::encode_error(id, err) } -> std::same_as<std::string>;
};

// Typed payload encode/decode requirements checked per message type
/// Typed payload encode/decode requirements of a JsonCodec for message type @p T.
///
/// Requires static `decode<T>(std::string_view)` -> `expected<T, RpcError>`,
/// `encode_result<T>(id, val)`, `encode_request<T>(id, method, val)` and
/// `encode_notification<T>(method, val)`, each returning `std::string`.
/// @tparam Codec Codec type satisfying JsonCodec.
/// @tparam T Payload type.
template <typename Codec, typename T>
concept JsonSerializable = JsonCodec<Codec> && requires(std::string_view json,
                                                        const RequestId& id,
                                                        std::string_view method,
                                                        const T& val) {
    { Codec::template decode<T>(json) } -> std::same_as<expected<T, RpcError>>;
    { Codec::template encode_result<T>(id, val) } -> std::same_as<std::string>;
    { Codec::template encode_request<T>(id, method, val) } -> std::same_as<std::string>;
    { Codec::template encode_notification<T>(method, val) } -> std::same_as<std::string>;
};

} // namespace buraq
// ---- end buraq/protocol/jsonrpc.hpp ----

// ---- begin buraq/protocol/schema_v1.hpp ----
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// ACP v1 protocol types. Field names mirror the wire format (camelCase) so codecs can reflect them
// directly. Open string enums (stop reasons, tool kinds, statuses, ...) are kept as std::string so
// values introduced by newer protocol revisions round-trip instead of failing to decode.
/// ACP v1 protocol types and helpers.
///
/// Field names mirror the wire format (camelCase) so codecs can reflect them directly. Open string
/// enums (stop reasons, tool kinds, statuses, ...) are kept as `std::string` so values introduced by
/// newer protocol revisions round-trip instead of failing to decode.
namespace buraq::v1 {

/// ACP protocol version implemented by this SDK (sent/expected in `initialize`).
inline constexpr std::uint32_t kProtocolVersion = 1;

// Presence-only capability object (`{}` on the wire means "supported")
/// Presence-only capability object.
///
/// Serialized as `{}`; its presence (in an `std::optional`) means the capability is supported.
struct CapabilityMarker {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CapabilityMarker&) const = default;
};

// --- Capabilities & Metadata ---
/// File-system capabilities advertised by the client (`clientCapabilities.fs`).
struct FileSystemCapabilities {
    /// Client supports `fs/read_text_file`. Defaults to `false`.
    bool readTextFile{false};
    /// Client supports `fs/write_text_file`. Defaults to `false`.
    bool writeTextFile{false};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const FileSystemCapabilities&) const = default;
};

/// Client support for session config option kinds (`clientCapabilities.session.configOptions`).
struct SessionConfigOptionsCapabilities {
    /// Present when the client supports `boolean` config options; optional.
    std::optional<CapabilityMarker> boolean{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionConfigOptionsCapabilities&) const = default;
};

/// Session-related capabilities advertised by the client (`clientCapabilities.session`).
struct ClientSessionCapabilities {
    /// Present when the client supports compaction updates; optional.
    std::optional<CapabilityMarker> compaction{};
    /// Config option kinds the client supports; optional.
    std::optional<SessionConfigOptionsCapabilities> configOptions{};
    /// Present when the client supports `notice` session updates; optional.
    std::optional<CapabilityMarker> notices{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ClientSessionCapabilities&) const = default;
};

/// Authentication capabilities advertised by the client (`clientCapabilities.auth`).
struct ClientAuthCapabilities {
    /// Client supports terminal-based authentication methods. Defaults to `false`.
    bool terminal{false};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ClientAuthCapabilities&) const = default;
};

/// Elicitation modes supported by the client (`clientCapabilities.elicitation`).
struct ElicitationCapabilities {
    /// Present when the client supports form-mode elicitation; optional.
    std::optional<CapabilityMarker> form{};
    /// Present when the client supports URL-mode elicitation; optional.
    std::optional<CapabilityMarker> url{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ElicitationCapabilities&) const = default;
};

/// Capabilities the client advertises in the `initialize` request.
struct ClientCapabilities {
    /// Alias of @ref FileSystemCapabilities.
    using FsCapabilities = FileSystemCapabilities;
    /// File-system capabilities; optional (absent means none).
    std::optional<FileSystemCapabilities> fs{};
    /// Client supports the `terminal/*` methods. Defaults to `false`.
    bool terminal{false};
    /// Session-related capabilities; optional.
    std::optional<ClientSessionCapabilities> session{};
    /// Authentication capabilities; optional.
    std::optional<ClientAuthCapabilities> auth{};
    /// Supported elicitation modes; optional (absent means no elicitation).
    std::optional<ElicitationCapabilities> elicitation{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ClientCapabilities&) const = default;
};

/// Content types the agent accepts in `session/prompt` (`agentCapabilities.promptCapabilities`).
///
/// Text and resource links are always supported; these flags opt into the rest.
struct PromptCapabilities {
    /// Agent accepts image content in prompts. Defaults to `false`.
    bool image{false};
    /// Agent accepts audio content in prompts. Defaults to `false`.
    bool audio{false};
    /// Agent accepts embedded resources in prompts. Defaults to `false`.
    bool embeddedContext{false};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PromptCapabilities&) const = default;
};

/// MCP transports the agent can connect to (`agentCapabilities.mcpCapabilities`).
///
/// Stdio MCP servers are always supported.
struct McpCapabilities {
    /// Agent can connect to HTTP MCP servers. Defaults to `false`.
    bool http{false};
    /// Agent can connect to SSE MCP servers. Defaults to `false`.
    bool sse{false};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const McpCapabilities&) const = default;
};

/// System-prompt modes supported by the agent beyond the default `append`.
struct SystemPromptModeCapabilities {
    /// Present when the agent supports `mode: "override"`; optional.
    std::optional<CapabilityMarker> override{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SystemPromptModeCapabilities&) const = default;
};

/// Agent support for client-supplied system prompts (`sessionCapabilities.systemPrompt`).
struct SystemPromptCapabilities {
    /// Supported system-prompt modes; optional.
    std::optional<SystemPromptModeCapabilities> mode{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SystemPromptCapabilities&) const = default;
};

// `delete_` is serialized as "delete" (a C++ keyword)
/// Optional session methods/features supported by the agent (`agentCapabilities.sessionCapabilities`).
///
/// The `delete_` member is serialized as `"delete"`.
struct SessionCapabilities {
    /// Present when the agent supports `session/list`; optional.
    std::optional<CapabilityMarker> list{};
    /// Present when the agent supports `session/delete`; serialized as `"delete"`; optional.
    std::optional<CapabilityMarker> delete_{};
    /// Present when the agent accepts `additionalDirectories` on session setup; optional.
    std::optional<CapabilityMarker> additionalDirectories{};
    /// System-prompt support; optional.
    std::optional<SystemPromptCapabilities> systemPrompt{};
    /// Present when the agent supports `session/resume`; optional.
    std::optional<CapabilityMarker> resume{};
    /// Present when the agent supports `session/close`; optional.
    std::optional<CapabilityMarker> close{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionCapabilities&) const = default;
};

/// Authentication capabilities advertised by the agent (`agentCapabilities.auth`).
struct AgentAuthCapabilities {
    /// Present when the agent supports `logout`; optional.
    std::optional<CapabilityMarker> logout{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AgentAuthCapabilities&) const = default;
};

/// Capabilities the agent advertises in the `initialize` response.
struct AgentCapabilities {
    /// Alias of @ref buraq::v1::PromptCapabilities.
    using PromptCapabilities = ::buraq::v1::PromptCapabilities;
    /// Agent supports `session/load`. Defaults to `false`.
    bool loadSession{false};
    /// Accepted prompt content types; optional.
    std::optional<PromptCapabilities> promptCapabilities{};
    /// Supported MCP transports; optional.
    std::optional<McpCapabilities> mcpCapabilities{};
    /// Optional session methods/features; optional.
    std::optional<SessionCapabilities> sessionCapabilities{};
    /// Authentication capabilities; optional.
    std::optional<AgentAuthCapabilities> auth{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AgentCapabilities&) const = default;
};

/// Name/version information about a client or agent implementation.
struct ImplementationInfo {
    /// Programmatic name of the implementation.
    std::string name;
    /// Version string of the implementation.
    std::string version;
    /// Optional human-readable display name.
    std::optional<std::string> title{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ImplementationInfo&) const = default;
};

// --- Authentication (tagged union discriminated by "type"; untagged = agent-handled) ---
/// Authentication method handled entirely by the agent.
///
/// Alternative of @ref AuthMethod selected when the `type` field is absent (or `"agent"`).
struct AuthMethodAgent {
    /// Unique identifier of the method, passed back in `authenticate`.
    std::string id;
    /// Human-readable name of the method.
    std::string name;
    /// Optional description of the method.
    std::optional<std::string> description{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AuthMethodAgent&) const = default;
};

/// Authentication method performed interactively in a terminal (`type: "terminal"`).
///
/// Requires the client to advertise `clientCapabilities.auth.terminal`.
struct AuthMethodTerminal {
    /// Discriminator; always `"terminal"` on the wire.
    std::string_view type{"terminal"};
    /// Unique identifier of the method, passed back in `authenticate`.
    std::string id;
    /// Human-readable name of the method.
    std::string name;
    /// Optional description of the method.
    std::optional<std::string> description{};
    /// Optional extra command-line arguments for the terminal login flow.
    std::optional<std::vector<std::string>> args{};
    /// Optional extra environment variables for the terminal login flow.
    std::optional<std::map<std::string, std::string>> env{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AuthMethodTerminal&) const = default;
};

/// An authentication method advertised in the `initialize` result.
///
/// Discriminated by `type`; see @ref variant_tag<AuthMethod>.
using AuthMethod = std::variant<
    AuthMethodAgent,
    AuthMethodTerminal
>;

/// Parameters of the `authenticate` request.
struct AuthenticateParams {
    /// Identifier of an auth method advertised in the `initialize` result; required.
    std::string methodId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AuthenticateParams&) const = default;
};

/// Result of the `authenticate` request (empty object).
struct AuthenticateResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AuthenticateResult&) const = default;
};

/// Parameters of the `logout` request (empty object).
struct LogoutParams {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const LogoutParams&) const = default;
};

/// Result of the `logout` request (empty object).
struct LogoutResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const LogoutResult&) const = default;
};

// --- Lifecycle: initialize ---
/// Parameters of the `initialize` request (client -> agent).
struct InitializeParams {
    /// Latest protocol version supported by the client. Defaults to @ref kProtocolVersion.
    std::uint32_t protocolVersion{kProtocolVersion};
    /// Capabilities supported by the client.
    ClientCapabilities clientCapabilities{};
    /// Optional information about the client implementation.
    std::optional<ImplementationInfo> clientInfo{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const InitializeParams&) const = default;
};

/// Result of the `initialize` request.
struct InitializeResult {
    /// Protocol version chosen by the agent. Defaults to @ref kProtocolVersion.
    std::uint32_t protocolVersion{kProtocolVersion};
    /// Capabilities supported by the agent.
    AgentCapabilities agentCapabilities{};
    /// Authentication methods the agent supports; empty when none are required.
    std::vector<AuthMethod> authMethods{};
    /// Optional information about the agent implementation.
    std::optional<ImplementationInfo> agentInfo{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const InitializeResult&) const = default;
};

// --- Content Blocks (tagged union discriminated by "type") ---
/// Optional hints attached to content blocks (mirrors MCP annotations).
struct Annotations {
    /// Optional intended audience: `"user"` and/or `"assistant"`.
    std::optional<std::vector<std::string>> audience{}; // "user", "assistant"
    /// Optional last-modified timestamp (ISO 8601).
    std::optional<std::string> lastModified{};
    /// Optional importance, from 0 (least) to 1 (most).
    std::optional<double> priority{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const Annotations&) const = default;
};

/// Plain text content block (`type: "text"`).
struct TextContent {
    /// Discriminator; always `"text"` on the wire.
    std::string_view type{"text"};
    /// The text.
    std::string text;
    /// Optional hints for the client about how to use or display the content.
    std::optional<Annotations> annotations{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TextContent&) const = default;
};

/// Image content block (`type: "image"`).
///
/// Requires `promptCapabilities.image` when sent in a prompt.
struct ImageContent {
    /// Discriminator; always `"image"` on the wire.
    std::string_view type{"image"};
    /// Base64-encoded image bytes.
    std::string data;     // Base64-encoded image bytes
    /// MIME type of the image, e.g. `"image/png"`.
    std::string mimeType; // e.g., "image/png"
    /// Optional URI identifying the image source.
    std::optional<std::string> uri{};
    /// Optional hints for the client about how to use or display the content.
    std::optional<Annotations> annotations{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ImageContent&) const = default;
};

/// Audio content block (`type: "audio"`).
///
/// Requires `promptCapabilities.audio` when sent in a prompt.
struct AudioContent {
    /// Discriminator; always `"audio"` on the wire.
    std::string_view type{"audio"};
    /// Base64-encoded audio bytes.
    std::string data;     // Base64-encoded audio bytes
    /// MIME type of the audio, e.g. `"audio/wav"`.
    std::string mimeType; // e.g., "audio/wav"
    /// Optional hints for the client about how to use or display the content.
    std::optional<Annotations> annotations{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AudioContent&) const = default;
};

/// Reference to a resource the recipient may fetch (`type: "resource_link"`).
struct ResourceLinkContent {
    /// Discriminator; always `"resource_link"` on the wire.
    std::string_view type{"resource_link"};
    /// URI of the linked resource.
    std::string uri;
    /// Name of the resource.
    std::string name;
    /// Optional MIME type of the resource.
    std::optional<std::string> mimeType{};
    /// Optional description of the resource.
    std::optional<std::string> description{};
    /// Optional human-readable title.
    std::optional<std::string> title{};
    /// Optional size of the resource in bytes.
    std::optional<std::int64_t> size{};
    /// Optional hints for the client about how to use or display the content.
    std::optional<Annotations> annotations{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ResourceLinkContent&) const = default;
};

// Text (`text`) or binary (`blob`, Base64) resource contents
/// Contents of an embedded resource.
///
/// Text resources set `text`; binary resources set `blob` (Base64).
struct EmbeddedResourcePayload {
    /// URI of the resource.
    std::string uri;
    /// Optional MIME type of the resource.
    std::optional<std::string> mimeType{};
    /// Text contents; set for text resources.
    std::optional<std::string> text{};
    /// Base64-encoded binary contents; set for binary resources.
    std::optional<std::string> blob{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const EmbeddedResourcePayload&) const = default;
};

/// Resource embedded inline in a content block (`type: "resource"`).
///
/// Requires `promptCapabilities.embeddedContext` when sent in a prompt.
struct EmbeddedResourceContent {
    /// Discriminator; always `"resource"` on the wire.
    std::string_view type{"resource"};
    /// The embedded resource contents.
    EmbeddedResourcePayload resource;
    /// Optional hints for the client about how to use or display the content.
    std::optional<Annotations> annotations{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const EmbeddedResourceContent&) const = default;
};

/// A content block (text, image, audio, resource link or embedded resource).
///
/// Discriminated by `type`; see @ref variant_tag<ContentBlock>.
using ContentBlock = std::variant<
    TextContent,
    ImageContent,
    AudioContent,
    ResourceLinkContent,
    EmbeddedResourceContent
>;

// --- MCP servers (tagged union discriminated by "type"; untagged = stdio) ---
/// Environment variable passed to a launched process (MCP server or terminal).
struct EnvVariable {
    /// Variable name.
    std::string name;
    /// Variable value.
    std::string value;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const EnvVariable&) const = default;
};

/// HTTP header sent to an HTTP/SSE MCP server.
struct HttpHeader {
    /// Header name.
    std::string name;
    /// Header value.
    std::string value;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const HttpHeader&) const = default;
};

/// MCP server reached over streamable HTTP (`type: "http"`).
///
/// Requires `mcpCapabilities.http`.
struct McpServerHttp {
    /// Discriminator; always `"http"` on the wire.
    std::string_view type{"http"};
    /// Human-readable server name.
    std::string name;
    /// URL of the MCP server.
    std::string url;
    /// HTTP headers to send with each request; may be empty.
    std::vector<HttpHeader> headers{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const McpServerHttp&) const = default;
};

/// MCP server reached over Server-Sent Events (`type: "sse"`).
///
/// Requires `mcpCapabilities.sse`.
struct McpServerSse {
    /// Discriminator; always `"sse"` on the wire.
    std::string_view type{"sse"};
    /// Human-readable server name.
    std::string name;
    /// URL of the MCP server.
    std::string url;
    /// HTTP headers to send with each request; may be empty.
    std::vector<HttpHeader> headers{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const McpServerSse&) const = default;
};

/// MCP server launched as a subprocess speaking stdio.
///
/// Alternative of @ref McpServer selected when the `type` field is absent.
struct McpServerStdio {
    /// Human-readable server name.
    std::string name;
    /// Path of the executable to launch.
    std::string command;
    /// Command-line arguments; may be empty.
    std::vector<std::string> args{};
    /// Environment variables to set; may be empty.
    std::vector<EnvVariable> env{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const McpServerStdio&) const = default;
};

/// An MCP server configuration passed in session setup requests.
///
/// Discriminated by `type`; stdio when absent. See @ref variant_tag<McpServer>.
using McpServer = std::variant<
    McpServerHttp,
    McpServerSse,
    McpServerStdio
>;

// --- Session configuration ---
/// A mode the agent can operate in (e.g. "ask", "code").
struct SessionMode {
    /// Unique identifier of the mode.
    std::string id;
    /// Human-readable name of the mode.
    std::string name;
    /// Optional description of the mode.
    std::optional<std::string> description{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionMode&) const = default;
};

/// Current and available session modes, returned when a session is created/loaded.
struct SessionModeState {
    /// Identifier of the active mode.
    std::string currentModeId;
    /// Modes the session can switch to.
    std::vector<SessionMode> availableModes{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionModeState&) const = default;
};

// UNSTABLE: session models are not part of the stable ACP v1 schema
/// A model the agent can use for a session.
///
/// @note Unstable in ACP: not part of the stable ACP v1 schema.
struct ModelInfo {
    /// Unique identifier of the model.
    std::string modelId;
    /// Human-readable name of the model.
    std::string name;
    /// Optional description of the model.
    std::optional<std::string> description{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ModelInfo&) const = default;
};

// UNSTABLE: session models are not part of the stable ACP v1 schema
/// Current and available models of a session.
///
/// @note Unstable in ACP: not part of the stable ACP v1 schema.
struct SessionModelState {
    /// Identifier of the active model.
    std::string currentModelId;
    /// Models the session can switch to.
    std::vector<ModelInfo> availableModels{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionModelState&) const = default;
};

// Value of a config option: a value id (`type: "select"`) or a boolean (`type: "boolean"`)
/// Value of a config option: a value id (`type: "select"`) or a boolean (`type: "boolean"`).
using SessionConfigValue = std::variant<std::string, bool>;

/// A selectable value of a `select` config option.
struct SessionConfigSelectOption {
    /// Value identifier, sent back in `session/set_config_option`.
    std::string value;
    /// Human-readable name of the value.
    std::string name;
    /// Optional description of the value.
    std::optional<std::string> description{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionConfigSelectOption&) const = default;
};

// Entry of a select option list: either an option (`value` set) or a group (`group` + `options` set)
/// Entry of a `select` config option's option list.
///
/// Either a single option (`value` set) or a named group (`group` and `options` set).
struct SessionConfigSelectItem {
    /// Value identifier; set when this entry is a single option.
    std::optional<std::string> value{};
    /// Human-readable name of the option or group.
    std::string name;
    /// Optional description of the option.
    std::optional<std::string> description{};
    /// Group identifier; set when this entry is a group.
    std::optional<std::string> group{};
    /// Options of the group; set when this entry is a group.
    std::optional<std::vector<SessionConfigSelectOption>> options{};

    /// Whether this entry is a group rather than a single option.
    /// @return `true` when `group` is set.
    [[nodiscard]] bool is_group() const noexcept { return group.has_value(); }
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionConfigSelectItem&) const = default;
};

/// A session configuration option exposed by the agent (e.g. mode, model, thought level).
struct SessionConfigOption {
    /// Unique identifier of the option, used in `session/set_config_option`.
    std::string id;
    /// Human-readable name of the option.
    std::string name;
    /// Optional description of the option.
    std::optional<std::string> description{};
    /// Optional semantic category: `"mode"`, `"model"`, `"model_config"`, `"thought_level"`, ... (open set).
    std::optional<std::string> category{}; // "mode", "model", "model_config", "thought_level", ...
    /// Option kind: `"select"` (default) or `"boolean"`.
    std::string type{"select"};            // "select" or "boolean"
    /// Current value: a value id for `select`, a `bool` for `boolean`.
    SessionConfigValue currentValue{std::string{}};
    /// Selectable values; present for `select` options.
    std::optional<std::vector<SessionConfigSelectItem>> options{}; // present for "select"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionConfigOption&) const = default;
};

/// Client-supplied system prompt for a session.
///
/// Requires `sessionCapabilities.systemPrompt`.
struct SystemPrompt {
    /// How the prompt is applied: `"append"` (default) or `"override"`.
    std::string mode{"append"}; // "append", "override"
    /// System prompt content blocks.
    std::vector<ContentBlock> content{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SystemPrompt&) const = default;
};

// --- Sessions: new, load, resume, list, delete, close, mode, config, prompt, cancel ---
/// Parameters of the `session/new` request.
struct SessionNewParams {
    /// Working directory of the session; MUST be an absolute path.
    std::string cwd;
    /// MCP servers the agent should connect to; may be empty.
    std::vector<McpServer> mcpServers{};
    /// Optional extra workspace directories (absolute paths); requires `sessionCapabilities.additionalDirectories`.
    std::optional<std::vector<std::string>> additionalDirectories{};
    /// Optional client-supplied system prompt.
    std::optional<SystemPrompt> systemPrompt{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionNewParams&) const = default;
};

/// Result of the `session/new` request.
struct SessionNewResult {
    /// Identifier of the newly created session.
    std::string sessionId;
    /// Optional session mode state.
    std::optional<SessionModeState> modes{};
    /// Optional session configuration options.
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    /// Optional session model state.
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    std::optional<SessionModelState> models{}; // UNSTABLE
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionNewResult&) const = default;
};

/// Parameters of the `session/load` request.
///
/// Requires `agentCapabilities.loadSession`; the agent replays the conversation via `session/update`.
struct SessionLoadParams {
    /// Identifier of the session to load.
    std::string sessionId;
    /// Working directory of the session; MUST be an absolute path.
    std::string cwd;
    /// MCP servers the agent should connect to; may be empty.
    std::vector<McpServer> mcpServers{};
    /// Optional extra workspace directories (absolute paths).
    std::optional<std::vector<std::string>> additionalDirectories{};
    /// Optional client-supplied system prompt.
    std::optional<SystemPrompt> systemPrompt{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionLoadParams&) const = default;
};

/// Result of the `session/load` request.
struct SessionLoadResult {
    /// Optional session mode state.
    std::optional<SessionModeState> modes{};
    /// Optional session configuration options.
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    /// Optional session model state.
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    std::optional<SessionModelState> models{}; // UNSTABLE
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionLoadResult&) const = default;
};

/// Parameters of the `session/resume` request.
///
/// Requires `sessionCapabilities.resume`; unlike `session/load`, history is not replayed.
struct SessionResumeParams {
    /// Identifier of the session to resume.
    std::string sessionId;
    /// Working directory of the session; MUST be an absolute path.
    std::string cwd;
    /// Optional MCP servers the agent should connect to.
    std::optional<std::vector<McpServer>> mcpServers{};
    /// Optional extra workspace directories (absolute paths).
    std::optional<std::vector<std::string>> additionalDirectories{};
    /// Optional client-supplied system prompt.
    std::optional<SystemPrompt> systemPrompt{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionResumeParams&) const = default;
};

/// Result of the `session/resume` request.
struct SessionResumeResult {
    /// Optional session mode state.
    std::optional<SessionModeState> modes{};
    /// Optional session configuration options.
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    /// Optional session model state.
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    std::optional<SessionModelState> models{}; // UNSTABLE
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionResumeResult&) const = default;
};

/// Parameters of the `session/list` request.
///
/// Requires `sessionCapabilities.list`.
struct SessionListParams {
    /// Optional filter: only sessions with this working directory (absolute path).
    std::optional<std::string> cwd{};
    /// Optional opaque pagination cursor from a previous `nextCursor`.
    std::optional<std::string> cursor{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionListParams&) const = default;
};

/// Summary of a session as returned by `session/list`.
struct SessionInfo {
    /// Identifier of the session.
    std::string sessionId;
    /// Working directory of the session.
    std::string cwd;
    /// Optional extra workspace directories of the session.
    std::optional<std::vector<std::string>> additionalDirectories{};
    /// Optional human-readable title.
    std::optional<std::string> title{};
    /// Optional last-activity timestamp (ISO 8601).
    std::optional<std::string> updatedAt{}; // ISO 8601
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionInfo&) const = default;
};

/// Result of the `session/list` request.
struct SessionListResult {
    /// Sessions in this page.
    std::vector<SessionInfo> sessions{};
    /// Cursor for the next page; absent when there are no more results.
    std::optional<std::string> nextCursor{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionListResult&) const = default;
};

/// Parameters of the `session/delete` request.
///
/// Requires `sessionCapabilities.delete`.
struct SessionDeleteParams {
    /// Identifier of the session to delete.
    std::string sessionId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionDeleteParams&) const = default;
};

/// Result of the `session/delete` request (empty object).
struct SessionDeleteResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionDeleteResult&) const = default;
};

/// Parameters of the `session/close` request.
///
/// Requires `sessionCapabilities.close`.
struct SessionCloseParams {
    /// Identifier of the session to close.
    std::string sessionId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionCloseParams&) const = default;
};

/// Result of the `session/close` request (empty object).
struct SessionCloseResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionCloseResult&) const = default;
};

/// Parameters of the `session/set_mode` request.
struct SessionSetModeParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the mode to switch to.
    std::string modeId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetModeParams&) const = default;
};

/// Result of the `session/set_mode` request (empty object).
struct SessionSetModeResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetModeResult&) const = default;
};

/// Parameters of the `session/set_config_option` request.
struct SessionSetConfigOptionParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the config option to change.
    std::string configId;
    /// Optional value kind; `"boolean"` when `value` is a `bool`.
    std::optional<std::string> type{}; // "boolean" when `value` is a bool
    /// New value: a value id for `select`, a `bool` for `boolean`.
    SessionConfigValue value{std::string{}};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetConfigOptionParams&) const = default;
};

/// Result of the `session/set_config_option` request.
struct SessionSetConfigOptionResult {
    /// Full, updated set of the session's config options.
    std::vector<SessionConfigOption> configOptions{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetConfigOptionResult&) const = default;
};

// UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
/// Parameters of the `session/set_model` request.
///
/// @note Unstable in ACP: not part of the stable ACP v1 schema.
struct SessionSetModelParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the model to switch to.
    std::string modelId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetModelParams&) const = default;
};

// UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
/// Result of the `session/set_model` request (empty object).
///
/// @note Unstable in ACP: not part of the stable ACP v1 schema.
struct SessionSetModelResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionSetModelResult&) const = default;
};

/// Parameters of the `session/prompt` request.
struct SessionPromptParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Content blocks of the user's message.
    std::vector<ContentBlock> prompt;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionPromptParams&) const = default;
};

/// Result of the `session/prompt` request, sent when the turn ends.
struct SessionPromptResult {
    /// Why the turn ended: `"end_turn"` (default), `"max_tokens"`, `"max_turn_requests"`, `"refusal"` or `"cancelled"` (open set).
    std::string stopReason{"end_turn"}; // "end_turn", "max_tokens", "max_turn_requests", "refusal", "cancelled"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionPromptResult&) const = default;
};

/// Parameters of the `session/cancel` notification (client -> agent).
struct SessionCancelParams {
    /// Identifier of the session whose prompt turn should be cancelled.
    std::string sessionId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionCancelParams&) const = default;
};

// `$/cancel_request` (either direction): cancel an in-flight JSON-RPC request
/// Parameters of the `$/cancel_request` notification (either direction).
///
/// Asks the peer to cancel an in-flight JSON-RPC request.
struct CancelRequestParams {
    /// Identifier of the request to cancel. Defaults to `0`.
    RequestId requestId{std::int64_t{0}};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CancelRequestParams&) const = default;
};

// --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
/// `session/update` payload streaming a chunk of a user message (`sessionUpdate: "user_message_chunk"`).
///
/// Used mainly when replaying history in `session/load`.
struct UserMessageChunk {
    /// Discriminator; always `"user_message_chunk"` on the wire.
    std::string_view sessionUpdate{"user_message_chunk"};
    /// The content block being streamed.
    ContentBlock content;
    /// Optional identifier grouping chunks that belong to the same message.
    std::optional<std::string> messageId{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const UserMessageChunk&) const = default;
};

/// `session/update` payload streaming a chunk of the agent's response (`sessionUpdate: "agent_message_chunk"`).
struct AgentMessageChunk {
    /// Discriminator; always `"agent_message_chunk"` on the wire.
    std::string_view sessionUpdate{"agent_message_chunk"};
    /// The content block being streamed.
    ContentBlock content;
    /// Optional identifier grouping chunks that belong to the same message.
    std::optional<std::string> messageId{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AgentMessageChunk&) const = default;
};

/// `session/update` payload streaming a chunk of the agent's reasoning (`sessionUpdate: "agent_thought_chunk"`).
struct AgentThoughtChunk {
    /// Discriminator; always `"agent_thought_chunk"` on the wire.
    std::string_view sessionUpdate{"agent_thought_chunk"};
    /// The content block being streamed.
    ContentBlock content;
    /// Optional identifier grouping chunks that belong to the same message.
    std::optional<std::string> messageId{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AgentThoughtChunk&) const = default;
};

/// File location affected by a tool call (used for "follow along" UIs).
struct ToolCallLocation {
    /// Absolute path of the affected file.
    std::string path;
    /// Optional line number within the file.
    std::optional<std::uint32_t> line{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallLocation&) const = default;
};

/// Tool call content wrapping a regular content block (`type: "content"`).
struct ToolCallContentItem {
    /// Discriminator; always `"content"` on the wire.
    std::string_view type{"content"};
    /// The wrapped content block.
    ContentBlock content;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallContentItem&) const = default;
};

/// Tool call content describing a file modification (`type: "diff"`).
struct ToolCallDiffItem {
    /// Discriminator; always `"diff"` on the wire.
    std::string_view type{"diff"};
    /// Absolute path of the modified file.
    std::string path;
    /// Original text; absent for newly created files.
    std::optional<std::string> oldText{};
    /// New text after the modification.
    std::string newText;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallDiffItem&) const = default;
};

/// Tool call content embedding a client terminal's live output (`type: "terminal"`).
struct ToolCallTerminalItem {
    /// Discriminator; always `"terminal"` on the wire.
    std::string_view type{"terminal"};
    /// Identifier of the embedded terminal, as returned by `terminal/create`.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallTerminalItem&) const = default;
};

/// Content produced by a tool call (content block, diff or terminal).
///
/// Discriminated by `type`; see @ref variant_tag<ToolCallContent>.
using ToolCallContent = std::variant<
    ToolCallContentItem,
    ToolCallDiffItem,
    ToolCallTerminalItem
>;

// `sessionUpdate: "tool_call"` (the spec's `ToolCall`): announces a new tool call
/// `session/update` payload announcing a new tool call (`sessionUpdate: "tool_call"`).
///
/// This is the spec's `ToolCall`; see also the @ref ToolCall alias.
struct ToolCallUpdate {
    /// Discriminator; always `"tool_call"` on the wire.
    std::string_view sessionUpdate{"tool_call"};
    /// Unique identifier of the tool call within the session.
    std::string toolCallId;
    /// Human-readable title of the tool call.
    std::string title;
    /// Optional programmatic name of the invoked tool.
    std::optional<std::string> name{};
    /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
    std::optional<std::string> kind{};   // "read", "edit", "delete", "move", "search", "execute", "think", "fetch", "switch_mode", "other"
    /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
    std::optional<std::string> status{}; // "pending", "in_progress", "completed", "failed"
    /// Optional content produced by the tool call.
    std::optional<std::vector<ToolCallContent>> content{};
    /// Optional file locations affected by the tool call.
    std::optional<std::vector<ToolCallLocation>> locations{};
    /// Optional raw input parameters sent to the tool (arbitrary JSON).
    std::optional<RawJson> rawInput{};
    /// Optional raw output returned by the tool (arbitrary JSON).
    std::optional<RawJson> rawOutput{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallUpdate&) const = default;
};

/// Spec-named alias of @ref ToolCallUpdate (the `"tool_call"` session update).
using ToolCall = ToolCallUpdate;

// `sessionUpdate: "tool_call_update"` (the spec's `ToolCallUpdate`): patches an existing tool call
/// `session/update` payload patching an existing tool call (`sessionUpdate: "tool_call_update"`).
///
/// This is the spec's `ToolCallUpdate`; only `toolCallId` is required and absent fields are left unchanged.
struct ToolCallStatusUpdate {
    /// Discriminator; always `"tool_call_update"` on the wire.
    std::string_view sessionUpdate{"tool_call_update"};
    /// Unique identifier of the tool call within the session.
    std::string toolCallId;
    /// Human-readable title of the tool call. Optional.
    std::optional<std::string> title{};
    /// Optional programmatic name of the invoked tool.
    std::optional<std::string> name{};
    /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
    std::optional<std::string> kind{};
    /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
    std::optional<std::string> status{};
    /// Optional content produced by the tool call. Replaces previous content when set.
    std::optional<std::vector<ToolCallContent>> content{};
    /// Optional file locations affected by the tool call.
    std::optional<std::vector<ToolCallLocation>> locations{};
    /// Optional raw input parameters sent to the tool (arbitrary JSON).
    std::optional<RawJson> rawInput{};
    /// Optional raw output returned by the tool (arbitrary JSON).
    std::optional<RawJson> rawOutput{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ToolCallStatusUpdate&) const = default;
};

/// One task of an agent execution plan.
struct PlanEntry {
    /// Human-readable description of the task.
    std::string content;
    /// Relative priority: `"high"`, `"medium"` or `"low"`.
    std::string priority; // "high", "medium", "low"
    /// Execution status: `"pending"`, `"in_progress"` or `"completed"`.
    std::string status;   // "pending", "in_progress", "completed"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PlanEntry&) const = default;
};

/// `session/update` payload carrying the agent's execution plan (`sessionUpdate: "plan"`).
struct PlanUpdate {
    /// Discriminator; always `"plan"` on the wire.
    std::string_view sessionUpdate{"plan"};
    /// Complete list of plan entries; replaces any previously sent plan.
    std::vector<PlanEntry> entries{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PlanUpdate&) const = default;
};

// Unstructured command input: free text shown as a hint when the user types the command
/// Unstructured input of a slash command.
///
/// Free text shown as a hint when the user types the command.
struct AvailableCommandInput {
    /// Hint text shown to the user for the command's input.
    std::string hint;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AvailableCommandInput&) const = default;
};

/// A slash command the agent makes available to the user.
struct AvailableCommand {
    /// Command name (e.g. `create_plan`, invoked as `/create_plan`).
    std::string name;
    /// Human-readable description of the command.
    std::string description;
    /// Optional input specification; absent when the command takes no input.
    std::optional<AvailableCommandInput> input{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AvailableCommand&) const = default;
};

/// `session/update` payload listing available slash commands (`sessionUpdate: "available_commands_update"`).
struct AvailableCommandsUpdate {
    /// Discriminator; always `"available_commands_update"` on the wire.
    std::string_view sessionUpdate{"available_commands_update"};
    /// Complete list of currently available commands.
    std::vector<AvailableCommand> availableCommands{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const AvailableCommandsUpdate&) const = default;
};

/// `session/update` payload reporting a session mode change (`sessionUpdate: "current_mode_update"`).
struct CurrentModeUpdate {
    /// Discriminator; always `"current_mode_update"` on the wire.
    std::string_view sessionUpdate{"current_mode_update"};
    /// Identifier of the now-active mode.
    std::string currentModeId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CurrentModeUpdate&) const = default;
};

/// `session/update` payload carrying the session's config options (`sessionUpdate: "config_option_update"`).
struct ConfigOptionUpdate {
    /// Discriminator; always `"config_option_update"` on the wire.
    std::string_view sessionUpdate{"config_option_update"};
    /// Full, updated set of the session's config options.
    std::vector<SessionConfigOption> configOptions{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const ConfigOptionUpdate&) const = default;
};

/// `session/update` payload updating session metadata (`sessionUpdate: "session_info_update"`).
struct SessionInfoUpdate {
    /// Discriminator; always `"session_info_update"` on the wire.
    std::string_view sessionUpdate{"session_info_update"};
    /// Optional new session title.
    std::optional<std::string> title{};
    /// Optional new last-activity timestamp (ISO 8601).
    std::optional<std::string> updatedAt{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionInfoUpdate&) const = default;
};

/// Monetary cost reported in a usage update.
struct Cost {
    /// Amount in `currency` units. Defaults to `0.0`.
    double amount{0.0};
    /// ISO 4217 currency code, e.g. `"USD"`.
    std::string currency; // ISO 4217, e.g. "USD"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const Cost&) const = default;
};

/// `session/update` payload reporting context-window usage (`sessionUpdate: "usage_update"`).
struct UsageUpdate {
    /// Discriminator; always `"usage_update"` on the wire.
    std::string_view sessionUpdate{"usage_update"};
    /// Tokens currently in the context window. Defaults to `0`.
    std::uint64_t used{0}; // tokens currently in context
    /// Size of the context window in tokens. Defaults to `0`.
    std::uint64_t size{0}; // context window size in tokens
    /// Optional cumulative cost of the session.
    std::optional<Cost> cost{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const UsageUpdate&) const = default;
};

/// `session/update` payload carrying a user-facing notice (`sessionUpdate: "notice"`).
///
/// Sent only to clients advertising `clientCapabilities.session.notices`.
struct NoticeUpdate {
    /// Discriminator; always `"notice"` on the wire.
    std::string_view sessionUpdate{"notice"};
    /// Severity: `"info"` (default), `"warning"` or `"error"`.
    std::string severity{"info"}; // "info", "warning", "error"
    /// Short notice title.
    std::string title;
    /// Optional longer description.
    std::optional<std::string> description{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const NoticeUpdate&) const = default;
};

/// `session/update` payload reporting context compaction progress (`sessionUpdate: "compaction_update"`).
///
/// Sent only to clients advertising `clientCapabilities.session.compaction`.
struct CompactionUpdate {
    /// Discriminator; always `"compaction_update"` on the wire.
    std::string_view sessionUpdate{"compaction_update"};
    /// Identifier of the compaction operation.
    std::string compactionId;
    /// Status: `"in_progress"` (default), `"completed"`, `"failed"` or `"cancelled"`.
    std::string status{"in_progress"}; // "in_progress", "completed", "failed", "cancelled"
    /// Optional summary of the compacted context.
    std::optional<std::vector<ContentBlock>> summary{};
    /// Optional error message when the compaction failed.
    std::optional<std::string> error{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CompactionUpdate&) const = default;
};

/// `session/update` payload streaming a chunk of a compaction summary (`sessionUpdate: "compaction_summary_chunk"`).
struct CompactionSummaryChunk {
    /// Discriminator; always `"compaction_summary_chunk"` on the wire.
    std::string_view sessionUpdate{"compaction_summary_chunk"};
    /// Identifier of the compaction operation the chunk belongs to.
    std::string compactionId;
    /// The summary content being streamed.
    ContentBlock content;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CompactionSummaryChunk&) const = default;
};

/// Payload of a `session/update` notification.
///
/// Discriminated by `sessionUpdate`; see @ref variant_tag<SessionUpdatePayload>.
using SessionUpdatePayload = std::variant<
    UserMessageChunk,
    AgentMessageChunk,
    AgentThoughtChunk,
    ToolCallUpdate,
    ToolCallStatusUpdate,
    PlanUpdate,
    AvailableCommandsUpdate,
    CurrentModeUpdate,
    ConfigOptionUpdate,
    SessionInfoUpdate,
    UsageUpdate,
    NoticeUpdate,
    CompactionUpdate,
    CompactionSummaryChunk
>;

/// Parameters of the `session/update` notification (agent -> client).
struct SessionUpdateNotification {
    /// Identifier of the session the update belongs to.
    std::string sessionId;
    /// The update payload (discriminated by `sessionUpdate`).
    SessionUpdatePayload update;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const SessionUpdateNotification&) const = default;
};

// --- Permissions: session/request_permission (Agent -> Client request) ---
/// An option presented to the user in a `session/request_permission` request.
struct PermissionOption {
    /// Unique identifier of the option, returned in the selected outcome.
    std::string optionId;
    /// Human-readable label of the option.
    std::string name;
    /// Option kind: `"allow_once"`, `"allow_always"`, `"reject_once"` or `"reject_always"`.
    std::string kind; // "allow_once", "allow_always", "reject_once", "reject_always"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PermissionOption&) const = default;
};

/// Tool call details included in a `session/request_permission` request.
///
/// Same shape as a `tool_call_update` without the discriminator: only `toolCallId` is required.
struct PermissionToolCallInfo {
    /// Unique identifier of the tool call within the session.
    std::string toolCallId;
    /// Human-readable title of the tool call. Optional.
    std::optional<std::string> title{};
    /// Optional programmatic name of the invoked tool.
    std::optional<std::string> name{};
    /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
    std::optional<std::string> kind{};
    /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
    std::optional<std::string> status{};
    /// Optional content produced by the tool call. Replaces previous content when set.
    std::optional<std::vector<ToolCallContent>> content{};
    /// Optional file locations affected by the tool call.
    std::optional<std::vector<ToolCallLocation>> locations{};
    /// Optional raw input parameters sent to the tool (arbitrary JSON).
    std::optional<RawJson> rawInput{};
    /// Optional raw output returned by the tool (arbitrary JSON).
    std::optional<RawJson> rawOutput{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PermissionToolCallInfo&) const = default;
};

/// Parameters of the `session/request_permission` request (agent -> client).
struct RequestPermissionParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// The tool call that needs permission.
    PermissionToolCallInfo toolCall;
    /// Options to present to the user.
    std::vector<PermissionOption> options{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const RequestPermissionParams&) const = default;
};

/// Permission outcome when the prompt turn was cancelled (`outcome: "cancelled"`).
struct PermissionCancelledOutcome {
    /// Discriminator; always `"cancelled"` on the wire.
    std::string_view outcome{"cancelled"};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PermissionCancelledOutcome&) const = default;
};

/// Permission outcome when the user selected an option (`outcome: "selected"`).
struct PermissionSelectedOutcome {
    /// Discriminator; always `"selected"` on the wire.
    std::string_view outcome{"selected"};
    /// Identifier of the option the user selected.
    std::string optionId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const PermissionSelectedOutcome&) const = default;
};

/// Outcome of a `session/request_permission` request.
///
/// Discriminated by `outcome`; see @ref variant_tag<PermissionOutcome>.
using PermissionOutcome = std::variant<
    PermissionCancelledOutcome,
    PermissionSelectedOutcome
>;

/// Result of the `session/request_permission` request.
struct RequestPermissionResult {
    /// The user's decision.
    PermissionOutcome outcome;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const RequestPermissionResult&) const = default;
};

// --- Elicitation: elicitation/create (Agent -> Client request), elicitation/complete (notification) ---
// Flattened form of the spec's mode/scope unions so unknown modes are preserved verbatim:
//   mode "form": `requestedSchema` (restricted JSON Schema) is set
//   mode "url":  `elicitationId` and `url` are set
//   scope: `sessionId` (+ optional `toolCallId`) or `requestId`
/// Parameters of the `elicitation/create` request (agent -> client).
///
/// Flattened form of the spec's mode/scope unions so unknown modes are preserved verbatim:
/// - mode `"form"`: `requestedSchema` is set;
/// - mode `"url"`: `elicitationId` and `url` are set;
/// - scope: `sessionId` (+ optional `toolCallId`) or `requestId`.
struct CreateElicitationParams {
    /// Elicitation mode: `"form"` (default) or `"url"` (open set).
    std::string mode{"form"};
    /// Message explaining what is being requested.
    std::string message;
    /// Restricted JSON Schema of the requested form; set in form mode.
    std::optional<RawJson> requestedSchema{};
    /// Identifier of the elicitation; required in URL mode.
    std::optional<std::string> elicitationId{};
    /// URL the user should open; required in URL mode.
    std::optional<std::string> url{};
    /// Optional session scope of the elicitation.
    std::optional<std::string> sessionId{};
    /// Optional tool call scope (together with `sessionId`).
    std::optional<std::string> toolCallId{};
    /// Optional request scope: the JSON-RPC request the elicitation relates to.
    std::optional<RequestId> requestId{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CreateElicitationParams&) const = default;
};

/// Result of the `elicitation/create` request.
struct CreateElicitationResult {
    /// User action: `"accept"`, `"decline"` or `"cancel"` (default).
    std::string action{"cancel"}; // "accept", "decline", "cancel"
    /// Submitted form values when `action == "accept"`; optional.
    std::optional<RawJson> content{}; // submitted form values when action == "accept"
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CreateElicitationResult&) const = default;
};

/// Parameters of the `elicitation/complete` notification (agent -> client).
///
/// Signals that an out-of-band (URL mode) elicitation has finished.
struct CompleteElicitationParams {
    /// Identifier of the completed elicitation.
    std::string elicitationId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const CompleteElicitationParams&) const = default;
};

// --- Delegated Filesystem Tools (Agent -> Client requests) ---
/// Parameters of the `fs/read_text_file` request (agent -> client).
///
/// Requires `clientCapabilities.fs.readTextFile`.
struct FsReadTextFileParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Absolute path of the file to read.
    std::string path;
    /// Optional 1-based line to start reading from.
    std::optional<std::uint32_t> line{};
    /// Optional maximum number of lines to read.
    std::optional<std::uint32_t> limit{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const FsReadTextFileParams&) const = default;
};

/// Result of the `fs/read_text_file` request.
struct FsReadTextFileResult {
    /// The file contents read.
    std::string content;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const FsReadTextFileResult&) const = default;
};

/// Parameters of the `fs/write_text_file` request (agent -> client).
///
/// Requires `clientCapabilities.fs.writeTextFile`.
struct FsWriteTextFileParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Absolute path of the file to write.
    std::string path;
    /// Text to write to the file.
    std::string content;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const FsWriteTextFileParams&) const = default;
};

/// Result of the `fs/write_text_file` request (empty object).
struct FsWriteTextFileResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const FsWriteTextFileResult&) const = default;
};

// --- Delegated Terminal Tools (Agent -> Client requests) ---
/// Parameters of the `terminal/create` request (agent -> client).
///
/// Requires `clientCapabilities.terminal`.
struct TerminalCreateParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Command to run.
    std::string command;
    /// Command arguments; may be empty.
    std::vector<std::string> args{};
    /// Optional working directory (absolute path).
    std::optional<std::string> cwd{};
    /// Environment variables to set; may be empty.
    std::vector<EnvVariable> env{};
    /// Optional maximum number of output bytes to retain; older output is truncated beyond it.
    std::optional<std::uint64_t> outputByteLimit{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalCreateParams&) const = default;
};

/// Result of the `terminal/create` request.
struct TerminalCreateResult {
    /// Identifier of the created terminal.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalCreateResult&) const = default;
};

/// Parameters of the `terminal/output` request (agent -> client).
struct TerminalOutputParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the terminal, as returned by `terminal/create`.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalOutputParams&) const = default;
};

/// Exit status of a terminal's command.
struct TerminalExitStatus {
    /// Process exit code; absent if terminated by a signal.
    std::optional<int> exitCode{};
    /// Signal that terminated the process; absent on normal exit.
    std::optional<std::string> signal{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalExitStatus&) const = default;
};

/// Result of the `terminal/output` request.
struct TerminalOutputResult {
    /// Output captured so far.
    std::string output;
    /// Whether output was truncated due to `outputByteLimit`. Defaults to `false`.
    bool truncated{false};
    /// Exit status; present only once the command has exited.
    std::optional<TerminalExitStatus> exitStatus{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalOutputResult&) const = default;
};

/// Parameters of the `terminal/wait_for_exit` request (agent -> client).
struct TerminalWaitForExitParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the terminal, as returned by `terminal/create`.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalWaitForExitParams&) const = default;
};

/// Result of the `terminal/wait_for_exit` request: the command's exit status.
struct TerminalWaitForExitResult {
    /// Process exit code; absent if terminated by a signal.
    std::optional<int> exitCode{};
    /// Signal that terminated the process; absent on normal exit.
    std::optional<std::string> signal{};
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalWaitForExitResult&) const = default;
};

/// Parameters of the `terminal/kill` request (agent -> client).
///
/// Kills the command but keeps the terminal valid until released.
struct TerminalKillParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the terminal, as returned by `terminal/create`.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalKillParams&) const = default;
};

/// Result of the `terminal/kill` request (empty object).
struct TerminalKillResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalKillResult&) const = default;
};

/// Parameters of the `terminal/release` request (agent -> client).
///
/// Kills the command if still running and frees the terminal.
struct TerminalReleaseParams {
    /// Identifier of the target session.
    std::string sessionId;
    /// Identifier of the terminal, as returned by `terminal/create`.
    std::string terminalId;
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalReleaseParams&) const = default;
};

/// Result of the `terminal/release` request (empty object).
struct TerminalReleaseResult {
    /// Defaulted member-wise three-way comparison (also provides `==`).
    auto operator<=>(const TerminalReleaseResult&) const = default;
};

// --- Tagged-union discriminators (shared by all codecs) ---
// `untagged` names the alternative selected when the discriminator key is absent (-1: required).
/// Tagged-union discriminator metadata for a protocol `std::variant`, shared by all codecs.
///
/// Specializations provide `key` (the discriminator JSON field), `ids` (the tag value of each
/// alternative, in index order) and `untagged` (the alternative selected when `key` is absent,
/// or `-1` if the tag is required).
/// @tparam Variant The `std::variant` type being described.
template <typename Variant>
struct variant_tag;

/// Discriminator metadata for @ref ContentBlock (discriminated by `type`; the tag is required).
template <>
struct variant_tag<ContentBlock> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "type";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 5> ids{"text", "image", "audio", "resource_link", "resource"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = -1;
};

/// Discriminator metadata for @ref ToolCallContent (discriminated by `type`; the tag is required).
template <>
struct variant_tag<ToolCallContent> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "type";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 3> ids{"content", "diff", "terminal"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = -1;
};

/// Discriminator metadata for @ref SessionUpdatePayload (discriminated by `sessionUpdate`; the tag is required).
template <>
struct variant_tag<SessionUpdatePayload> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "sessionUpdate";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 14> ids{
        "user_message_chunk", "agent_message_chunk", "agent_thought_chunk", "tool_call",
        "tool_call_update", "plan", "available_commands_update", "current_mode_update",
        "config_option_update", "session_info_update", "usage_update", "notice",
        "compaction_update", "compaction_summary_chunk"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = -1;
};

/// Discriminator metadata for @ref PermissionOutcome (discriminated by `outcome`; the tag is required).
template <>
struct variant_tag<PermissionOutcome> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "outcome";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 2> ids{"cancelled", "selected"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = -1;
};

/// Discriminator metadata for @ref McpServer (discriminated by `type`; a missing tag selects @ref McpServerStdio).
template <>
struct variant_tag<McpServer> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "type";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 3> ids{"http", "sse", "stdio"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = 2;
};

/// Discriminator metadata for @ref AuthMethod (discriminated by `type`; a missing tag selects @ref AuthMethodAgent).
template <>
struct variant_tag<AuthMethod> {
    /// JSON field holding the discriminator.
    static constexpr std::string_view key = "type";
    /// Discriminator value of each alternative, in `std::variant` index order.
    static constexpr std::array<std::string_view, 2> ids{"agent", "terminal"};
    /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
    static constexpr int untagged = 0;
};

// --- Semantic validation of inbound params (beyond JSON shape) ---
// Returns a human-readable reason when `params` violates an ACP v1 MUST; both base classes reply
// with -32602 (Invalid params) in that case.
/// Checks whether `path` is absolute.
///
/// Accepts POSIX paths (`/...`), paths starting with a backslash, and Windows drive paths
/// (`C:/...` or `C:\\...`).
/// @param path Path to check.
/// @return `true` if `path` is absolute; `false` otherwise (including when empty).
inline bool is_absolute_path(std::string_view path) noexcept {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return true;
    const auto drive = path.front();
    return path.size() >= 3 && ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) &&
           path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

/// Internal helpers used by the @ref validate overloads.
namespace detail {
/// Checks that a required identifier/string field is non-empty.
/// @param value Field value.
/// @param field Field name used in the error message.
/// @return `"<field> is required"` if `value` is empty, otherwise `std::nullopt`.
inline std::optional<std::string> require_id(std::string_view value, std::string_view field) {
    if (value.empty()) return std::string(field) + " is required";
    return std::nullopt;
}

/// Checks that a path field is absolute (see @ref is_absolute_path).
/// @param value Field value.
/// @param field Field name used in the error message.
/// @return `"<field> must be an absolute path"` if not absolute, otherwise `std::nullopt`.
inline std::optional<std::string> require_abs(std::string_view value, std::string_view field) {
    if (!is_absolute_path(value)) return std::string(field) + " must be an absolute path";
    return std::nullopt;
}

/// Checks that every entry of an optional path list is absolute.
/// @param values Optional list of paths; absent passes.
/// @param field Field name used in the error message.
/// @return The error for the first non-absolute entry, otherwise `std::nullopt`.
inline std::optional<std::string> require_abs(const std::optional<std::vector<std::string>>& values,
                                              std::string_view field) {
    if (values) {
        for (const auto& v : *values) {
            if (auto err = require_abs(v, field)) return err;
        }
    }
    return std::nullopt;
}

/// Returns the first error among several check results.
/// @tparam Checks Types convertible to `std::optional<std::string>`.
/// @param checks Already-evaluated check results, in priority order.
/// @return The first engaged result, or `std::nullopt` if all passed.
template <typename... Checks>
std::optional<std::string> first_error(Checks&&... checks) {
    std::optional<std::string> out{};
    ((out ? void() : void(out = std::forward<Checks>(checks))), ...);
    return out;
}
} // namespace detail

/// Semantic validation of inbound params beyond JSON shape (default: always valid).
///
/// Overloads for specific params types check ACP v1 MUST rules; the agent/client base classes
/// reply with `-32602` (Invalid params) when an error is returned.
/// @tparam T Params type.
/// @return `std::nullopt`; this generic overload accepts every value.
template <typename T>
std::optional<std::string> validate(const T&) {
    return std::nullopt;
}

/// Validates @ref AuthenticateParams (checks that `methodId` is non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const AuthenticateParams& p) {
    return detail::require_id(p.methodId, "methodId");
}
/// Validates @ref SessionNewParams (checks that `cwd` and every `additionalDirectories` entry are absolute paths).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionNewParams& p) {
    return detail::first_error(detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
/// Validates @ref SessionLoadParams (checks that `sessionId` is non-empty; `cwd` and every `additionalDirectories` entry are absolute paths).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionLoadParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
/// Validates @ref SessionResumeParams (checks that `sessionId` is non-empty; `cwd` and every `additionalDirectories` entry are absolute paths).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionResumeParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
/// Validates @ref SessionListParams (checks that `cwd`, when present, is an absolute path).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionListParams& p) {
    if (p.cwd) return detail::require_abs(*p.cwd, "cwd");
    return std::nullopt;
}
/// Validates @ref SessionDeleteParams (checks that `sessionId` is non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionDeleteParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
/// Validates @ref SessionCloseParams (checks that `sessionId` is non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionCloseParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
/// Validates @ref SessionSetModeParams (checks that `sessionId` and `modeId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionSetModeParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_id(p.modeId, "modeId"));
}
/// Validates @ref SessionSetConfigOptionParams (checks that `sessionId` and `configId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionSetConfigOptionParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.configId, "configId"));
}
/// Validates @ref SessionSetModelParams (checks that `sessionId` and `modelId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionSetModelParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.modelId, "modelId"));
}
/// Validates @ref SessionPromptParams (checks that `sessionId` is non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const SessionPromptParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
/// Validates @ref RequestPermissionParams (checks that `sessionId` and `toolCall.toolCallId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const RequestPermissionParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.toolCall.toolCallId, "toolCall.toolCallId"));
}
/// Validates @ref CreateElicitationParams (checks that in URL mode (`mode == "url"`), `elicitationId` and `url` are present and non-empty; other modes are accepted).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const CreateElicitationParams& p) {
    if (p.mode == "url") {
        return detail::first_error(detail::require_id(p.elicitationId.value_or(""), "elicitationId"),
                                   detail::require_id(p.url.value_or(""), "url"));
    }
    return std::nullopt;
}
/// Validates @ref FsReadTextFileParams (checks that `sessionId` is non-empty and `path` is an absolute path).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const FsReadTextFileParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
}
/// Validates @ref FsWriteTextFileParams (checks that `sessionId` is non-empty and `path` is an absolute path).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const FsWriteTextFileParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
}
/// Validates @ref TerminalCreateParams (checks that `sessionId` and `command` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const TerminalCreateParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.command, "command"));
}
/// Validates @ref TerminalOutputParams (checks that `sessionId` and `terminalId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const TerminalOutputParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
/// Validates @ref TerminalWaitForExitParams (checks that `sessionId` and `terminalId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const TerminalWaitForExitParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
/// Validates @ref TerminalKillParams (checks that `sessionId` and `terminalId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const TerminalKillParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
/// Validates @ref TerminalReleaseParams (checks that `sessionId` and `terminalId` are non-empty).
/// @param p Params to validate.
/// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
inline std::optional<std::string> validate(const TerminalReleaseParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}

} // namespace buraq::v1
// ---- end buraq/protocol/schema_v1.hpp ----

// ---- begin buraq/core/agent_base.hpp ----
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// Root namespace of the buraq Agent Client Protocol (ACP) v1 SDK.
namespace buraq {

// CRTP base for ACP agents. Every handler is optional and detected at compile time; requests for
// methods the derived class does not implement are answered with -32601 (Method not found).
//
// Client -> Agent requests:   on_initialize, on_authenticate, on_logout, on_session_new, on_session_load,
//                             on_session_resume, on_session_list, on_session_delete, on_session_close,
//                             on_session_set_mode, on_session_set_config_option, on_session_set_model (unstable),
//                             on_session_prompt / on_session_prompt_stream, on_ext_method
// Client -> Agent notifications: on_session_cancel, on_cancel_request, on_ext_notification
// Responses to agent-initiated requests: on_raw_response
/// CRTP base class for implementing an ACP v1 agent over JSON-RPC 2.0.
///
/// `AgentBase` is sans-I/O: feed it one inbound frame at a time with feed() and it decodes the message,
/// validates the parameters, dispatches to the matching `Derived` handler and writes the encoded reply to the
/// supplied outbound sink. Every handler is optional and detected at compile time with `requires`
/// expressions; requests for methods `Derived` does not implement are answered with `-32601` (Method not found).
/// Handlers that return `expected<Result, RpcError>` have the value encoded as the JSON-RPC `result`, or the
/// error encoded as the JSON-RPC `error`.
///
/// Optional `Derived` hooks (Client -> Agent requests):
/// - `expected<v1::InitializeResult, RpcError> on_initialize(const v1::InitializeParams&)` -- `initialize`.
///   When present, every other request is rejected with `-32600` (Invalid Request) until it succeeds.
/// - `expected<v1::AuthenticateResult, RpcError> on_authenticate(const v1::AuthenticateParams&)` -- `authenticate`
/// - `expected<v1::LogoutResult, RpcError> on_logout(const v1::LogoutParams&)` -- `logout`
/// - `expected<v1::SessionNewResult, RpcError> on_session_new(const v1::SessionNewParams&)` -- `session/new`
/// - `expected<v1::SessionLoadResult, RpcError> on_session_load(const v1::SessionLoadParams&)` -- `session/load`
/// - `expected<v1::SessionResumeResult, RpcError> on_session_resume(const v1::SessionResumeParams&)` -- `session/resume`
/// - `expected<v1::SessionListResult, RpcError> on_session_list(const v1::SessionListParams&)` -- `session/list`
/// - `expected<v1::SessionDeleteResult, RpcError> on_session_delete(const v1::SessionDeleteParams&)` -- `session/delete`
/// - `expected<v1::SessionCloseResult, RpcError> on_session_close(const v1::SessionCloseParams&)` -- `session/close`
/// - `expected<v1::SessionSetModeResult, RpcError> on_session_set_mode(const v1::SessionSetModeParams&)` -- `session/set_mode`
/// - `expected<v1::SessionSetConfigOptionResult, RpcError> on_session_set_config_option(const v1::SessionSetConfigOptionParams&)`
///   -- `session/set_config_option`
/// - `expected<v1::SessionSetModelResult, RpcError> on_session_set_model(const v1::SessionSetModelParams&)`
///   -- `session/set_model` (unstable in ACP)
/// - `generator<v1::SessionUpdatePayload> on_session_prompt_stream(const v1::SessionPromptParams&)` -- `session/prompt`
///   (only when `BURAQ_HAS_STD_GENERATOR`); each yielded payload is sent as a `session/update` notification and the
///   request completes with `stopReason = "end_turn"`. Takes precedence over `on_session_prompt`.
/// - `expected<v1::SessionPromptResult, RpcError> on_session_prompt(const v1::SessionPromptParams&, Sink&)` or
///   `on_session_prompt(const v1::SessionPromptParams&)` -- `session/prompt`; the two-argument form receives the
///   outbound sink so it can stream updates with emit_session_update() and delegate requests to the client.
/// - `expected<RawJson, RpcError> on_ext_method(std::string_view method, const RawJson& params)` -- any `_`-prefixed request
///
/// Optional `Derived` hooks (Client -> Agent notifications and responses):
/// - `void on_session_cancel(const v1::SessionCancelParams&)` -- `session/cancel`
/// - `void on_cancel_request(const v1::CancelRequestParams&)` -- `$/cancel_request`
/// - `void on_ext_notification(std::string_view method, const RawJson& params)` -- any `_`-prefixed notification
/// - `void on_raw_response(const RawEnvelope&)` -- responses to agent-initiated requests (request_permission(),
///   request_fs_read(), ...)
///
/// @code
/// class EchoAgent : public buraq::AgentBase<EchoAgent, buraq::GlazeCodec> {
/// public:
///     buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
///     on_initialize(const buraq::v1::InitializeParams&) {
///         return buraq::v1::InitializeResult{
///             .agentInfo = buraq::v1::ImplementationInfo{"buraq-echo", "0.1.0"}};
///     }
///
///     buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
///     on_session_new(const buraq::v1::SessionNewParams&) {
///         return buraq::v1::SessionNewResult{.sessionId = "sess_1"};
///     }
///
///     template <typename Sink>
///     buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
///     on_session_prompt(const buraq::v1::SessionPromptParams& params, Sink& sink) {
///         emit_session_update(buraq::v1::SessionUpdateNotification{
///             .sessionId = params.sessionId,
///             .update = buraq::v1::AgentMessageChunk{
///                 .content = buraq::v1::TextContent{.text = "Echo from buraq!"}}}, sink);
///         return buraq::v1::SessionPromptResult{.stopReason = "end_turn"};
///     }
/// };
///
/// int main() {
///     EchoAgent agent;
///     return buraq::transport::run_stdio_agent(agent);
/// }
/// @endcode
///
/// @tparam Derived The concrete agent type (CRTP); must publicly derive from `AgentBase<Derived, Codec>`.
/// @tparam Codec   JSON codec satisfying the JsonCodec concept (e.g. `GlazeCodec`, `NlohmannCodec`).
/// @note Not internally synchronized: call feed() and the outbound helpers from one thread at a time.
/// @see ClientBase
template <typename Derived, JsonCodec Codec>
class AgentBase {
protected:
    /// Returns `*this` downcast to the derived agent type.
    /// @return Mutable reference to the `Derived` object.
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    /// Returns `*this` downcast to the derived agent type (const overload).
    /// @return Const reference to the `Derived` object.
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    /// Writes an already-encoded JSON-RPC message to the outbound sink as raw bytes.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param wire Serialized JSON-RPC message.
    /// @param sink Outbound byte sink.
    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

public:
    /// Processes one inbound JSON-RPC frame from the client.
    /// Parses the envelope and dispatches it: responses go to `on_raw_response`, requests and notifications to the
    /// matching `Derived` hook. Replies (results and protocol errors such as `-32601`, `-32602` or `-32600` before
    /// `initialize`) are written to @p sink rather than returned.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param frame Bytes of a single JSON-RPC message (one NDJSON line, without the newline).
    /// @param sink  Outbound byte sink that receives any reply.
    /// @return Empty on success; an RpcError if the frame could not be parsed as a JSON-RPC envelope, or if a
    ///         known request method arrived without an `id`. No reply is emitted in those cases.
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) {
            return unexpected(env.error());
        }
        return dispatch_message(*env, std::forward<OutboundSink>(sink));
    }

    // True once `initialize` has completed successfully on this connection
    /// Reports whether `initialize` has completed successfully on this connection.
    /// @return `true` once `on_initialize` returned a value; always `false` if `Derived` has no `on_initialize`.
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    // --- Negotiated Client Capabilities (populated by `initialize`) ---
    /// Returns the client capabilities negotiated by `initialize`.
    /// @return The `clientCapabilities` sent by the client, or `std::nullopt` before a successful `initialize`.
    [[nodiscard]] const std::optional<v1::ClientCapabilities>& client_capabilities() const noexcept {
        return client_capabilities_;
    }

    /// Returns the client implementation info sent with `initialize`.
    /// @return The `clientInfo` sent by the client, or `std::nullopt` if absent or before a successful `initialize`.
    [[nodiscard]] const std::optional<v1::ImplementationInfo>& client_info() const noexcept {
        return client_info_;
    }

    /// Reports whether the client advertised `fs.readTextFile`.
    /// @return `true` if request_fs_read() (`fs/read_text_file`) may be used.
    [[nodiscard]] bool client_can_read_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->readTextFile;
    }

    /// Reports whether the client advertised `fs.writeTextFile`.
    /// @return `true` if request_fs_write() (`fs/write_text_file`) may be used.
    [[nodiscard]] bool client_can_write_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->writeTextFile;
    }

    /// Reports whether the client advertised the `terminal` capability.
    /// @return `true` if the `terminal/*` requests (request_terminal_create(), ...) may be used.
    [[nodiscard]] bool client_supports_terminal() const noexcept {
        return client_capabilities_ && client_capabilities_->terminal;
    }

    /// Reports whether the client advertised form-based elicitation (`elicitation.form`).
    /// @return `true` if form-mode request_elicitation() may be used.
    [[nodiscard]] bool client_supports_elicitation_form() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->form;
    }

    /// Reports whether the client advertised URL-based elicitation (`elicitation.url`).
    /// @return `true` if URL-mode request_elicitation() may be used.
    [[nodiscard]] bool client_supports_elicitation_url() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->url;
    }

    /// Reports whether the client advertised terminal authentication (`auth.terminal`).
    /// @return `true` if the client can run terminal-based auth methods.
    [[nodiscard]] bool client_supports_terminal_auth() const noexcept {
        return client_capabilities_ && client_capabilities_->auth && client_capabilities_->auth->terminal;
    }

    /// Reports whether the client advertised boolean session config options (`session.configOptions.boolean`).
    /// @return `true` if boolean config options may be offered to the client.
    [[nodiscard]] bool client_supports_boolean_config_options() const noexcept {
        return client_capabilities_ && client_capabilities_->session &&
               client_capabilities_->session->configOptions && client_capabilities_->session->configOptions->boolean;
    }

    // --- Notifications (Agent -> Client) ---
    /// Sends a `session/update` notification to the client.
    /// Encodes and emits a JSON-RPC notification for ACP method `session/update`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param update Session id and update payload to stream.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void emit_session_update(const v1::SessionUpdateNotification& update, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/update", update), std::forward<OutboundSink>(sink));
    }

    /// Tells the client that an elicitation (identified by `elicitationId`) has completed.
    /// Encodes and emits a JSON-RPC notification for ACP method `elicitation/complete`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Identifies the completed elicitation.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void notify_elicitation_complete(const v1::CompleteElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("elicitation/complete", params), std::forward<OutboundSink>(sink));
    }

    // Ask the client to abandon a request this agent sent earlier (`$/cancel_request`)
    /// Asks the client to abandon a request this agent sent earlier.
    /// Encodes and emits a JSON-RPC notification for ACP method `$/cancel_request`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Id of the agent-initiated request to cancel.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension notification
    /// Sends a custom extension notification to the client.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Delegated Client Requests (Agent -> Client); replies arrive via on_raw_response ---
    /// Asks the client for permission to run a tool call.
    /// Encodes and emits a JSON-RPC request for ACP method `session/request_permission`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, tool call and permission options.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void request_permission(const RequestId& id, const v1::RequestPermissionParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "session/request_permission", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to read a text file.
    /// Encodes and emits a JSON-RPC request for ACP method `fs/read_text_file`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, path and optional line range.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_can_read_text_file().
    template <typename OutboundSink>
    void request_fs_read(const RequestId& id, const v1::FsReadTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/read_text_file", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to write a text file.
    /// Encodes and emits a JSON-RPC request for ACP method `fs/write_text_file`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, path and content.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_can_write_text_file().
    template <typename OutboundSink>
    void request_fs_write(const RequestId& id, const v1::FsWriteTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/write_text_file", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to start a command in a new terminal.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/create`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, command, arguments and environment.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_create(const RequestId& id, const v1::TerminalCreateParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/create", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client for a terminal's current output.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/output`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_output(const RequestId& id, const v1::TerminalOutputParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/output", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to wait for a terminal's command to exit.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/wait_for_exit`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_wait_for_exit(const RequestId& id,
                                        const v1::TerminalWaitForExitParams& params,
                                        OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/wait_for_exit", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to kill a terminal's command.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/kill`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_kill(const RequestId& id, const v1::TerminalKillParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/kill", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to release a terminal and its resources.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/release`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_release(const RequestId& id, const v1::TerminalReleaseParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/release", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to collect structured input from the user.
    /// Encodes and emits a JSON-RPC request for ACP method `elicitation/create`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Elicitation mode, message and schema/URL.
    /// @param sink   Outbound byte sink.
    /// @note Check client_supports_elicitation_form() / client_supports_elicitation_url() first.
    template <typename OutboundSink>
    void request_elicitation(const RequestId& id, const v1::CreateElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "elicitation/create", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    /// Sends a custom extension request to the client.
    /// The reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller.
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void request_ext_method(const RequestId& id, std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, method, params), std::forward<OutboundSink>(sink));
    }

private:
    /// Set when `on_initialize` succeeds; gates all other requests when `Derived` implements `on_initialize`.
    bool initialized_{false};
    /// Client capabilities from the last successful `initialize`.
    std::optional<v1::ClientCapabilities> client_capabilities_{};
    /// Client implementation info from the last successful `initialize`.
    std::optional<v1::ImplementationInfo> client_info_{};

    /// Routes a parsed envelope to the matching `Derived` hook by method name.
    /// Handles the `initialize` gate, built-in ACP methods, `_`-prefixed extensions and the `-32601` fallback.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param env  Parsed inbound envelope.
    /// @param sink Outbound byte sink.
    /// @return Empty on success, or the error from handle_request().
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_message(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.is_response()) {
            if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
                self().on_raw_response(env);
            }
            return {};
        }

        if (env.method == "initialize") {
            if constexpr (requires(Derived& d, const v1::InitializeParams& p) { d.on_initialize(p); }) {
                return handle_request<v1::InitializeParams>(
                    env, sink, [this](Derived& d, const v1::InitializeParams& p, OutboundSink&) {
                        client_capabilities_ = p.clientCapabilities;
                        client_info_ = p.clientInfo;
                        auto result = d.on_initialize(p);
                        initialized_ = result.has_value();
                        if (!result) {
                            client_capabilities_.reset();
                            client_info_.reset();
                        }
                        return result;
                    });
            }
        }

        // Agents that implement `initialize` reject every other request until it succeeds
        if constexpr (requires(Derived& d, const v1::InitializeParams& p) { d.on_initialize(p); }) {
            if (!initialized_ && env.method != "initialize") {
                if (env.id.has_value()) {
                    emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_request(
                                                                "Connection not initialized: send `initialize` first")),
                               sink);
                }
                return {};
            }
        }

        if (env.method == "authenticate") {
            if constexpr (requires(Derived& d, const v1::AuthenticateParams& p) { d.on_authenticate(p); }) {
                return handle_request<v1::AuthenticateParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_authenticate(p); });
            }
        }

        if (env.method == "logout") {
            if constexpr (requires(Derived& d, const v1::LogoutParams& p) { d.on_logout(p); }) {
                return handle_request<v1::LogoutParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_logout(p); });
            }
        }

        if (env.method == "session/new") {
            if constexpr (requires(Derived& d, const v1::SessionNewParams& p) { d.on_session_new(p); }) {
                return handle_request<v1::SessionNewParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_new(p); });
            }
        }

        if (env.method == "session/load") {
            if constexpr (requires(Derived& d, const v1::SessionLoadParams& p) { d.on_session_load(p); }) {
                return handle_request<v1::SessionLoadParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_load(p); });
            }
        }

        if (env.method == "session/resume") {
            if constexpr (requires(Derived& d, const v1::SessionResumeParams& p) { d.on_session_resume(p); }) {
                return handle_request<v1::SessionResumeParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_resume(p); });
            }
        }

        if (env.method == "session/list") {
            if constexpr (requires(Derived& d, const v1::SessionListParams& p) { d.on_session_list(p); }) {
                return handle_request<v1::SessionListParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_list(p); });
            }
        }

        if (env.method == "session/delete") {
            if constexpr (requires(Derived& d, const v1::SessionDeleteParams& p) { d.on_session_delete(p); }) {
                return handle_request<v1::SessionDeleteParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_delete(p); });
            }
        }

        if (env.method == "session/close") {
            if constexpr (requires(Derived& d, const v1::SessionCloseParams& p) { d.on_session_close(p); }) {
                return handle_request<v1::SessionCloseParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_close(p); });
            }
        }

        if (env.method == "session/set_mode") {
            if constexpr (requires(Derived& d, const v1::SessionSetModeParams& p) { d.on_session_set_mode(p); }) {
                return handle_request<v1::SessionSetModeParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_mode(p); });
            }
        }

        if (env.method == "session/set_config_option") {
            if constexpr (requires(Derived& d, const v1::SessionSetConfigOptionParams& p) {
                d.on_session_set_config_option(p);
            }) {
                return handle_request<v1::SessionSetConfigOptionParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_config_option(p); });
            }
        }

        if (env.method == "session/set_model") {
            if constexpr (requires(Derived& d, const v1::SessionSetModelParams& p) { d.on_session_set_model(p); }) {
                return handle_request<v1::SessionSetModelParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_model(p); });
            }
        }

        if (env.method == "session/prompt") {
#if BURAQ_HAS_STD_GENERATOR
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p) {
                { d.on_session_prompt_stream(p) } -> std::same_as<generator<v1::SessionUpdatePayload>>;
            }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, OutboundSink& s) {
                        for (auto&& update : d.on_session_prompt_stream(p)) {
                            d.emit_session_update(v1::SessionUpdateNotification{
                                .sessionId = p.sessionId,
                                .update = std::forward<decltype(update)>(update),
                            }, s);
                        }
                        return expected<v1::SessionPromptResult, RpcError>(
                            v1::SessionPromptResult{.stopReason = "end_turn"});
                    });
            }
#endif
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p, OutboundSink& s) {
                d.on_session_prompt(p, s);
            }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, OutboundSink& s) { return d.on_session_prompt(p, s); });
            }
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p) { d.on_session_prompt(p); }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_prompt(p); });
            }
        }

        if (env.method == "session/cancel") {
            if constexpr (requires(Derived& d, const v1::SessionCancelParams& p) { d.on_session_cancel(p); }) {
                if (auto params = Codec::template decode<v1::SessionCancelParams>(env.params_json)) {
                    self().on_session_cancel(*params);
                }
            }
            return {};
        }

        if (env.method == "$/cancel_request") {
            if constexpr (requires(Derived& d, const v1::CancelRequestParams& p) { d.on_cancel_request(p); }) {
                if (auto params = Codec::template decode<v1::CancelRequestParams>(env.params_json)) {
                    self().on_cancel_request(*params);
                }
            }
            return {};
        }

        if (!env.method.empty() && env.method.front() == '_') {
            return dispatch_extension(env, sink);
        }

        if (env.id.has_value()) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
        }
        return {};
    }

    /// Dispatches a `_`-prefixed extension request or notification.
    /// Requests go to `on_ext_method` (or are answered with `-32601`); notifications go to `on_ext_notification`
    /// or are ignored.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param env  Parsed inbound envelope whose method starts with `_`.
    /// @param sink Outbound byte sink.
    /// @return Always empty.
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_extension(const RawEnvelope& env, OutboundSink& sink) {
        const RawJson params{std::string(env.params_json)};
        if (env.id.has_value()) {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError>>;
            }) {
                auto result = self().on_ext_method(env.method, params);
                if (!result) {
                    emit_bytes(Codec::encode_error(*env.id, result.error()), sink);
                } else {
                    emit_bytes(Codec::encode_result(*env.id, *result), sink);
                }
                return {};
            }
            emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
            return {};
        }
        if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) { d.on_ext_notification(m, p); }) {
            self().on_ext_notification(env.method, params);
        }
        return {};
    }

    /// Decodes, validates and answers a typed request.
    /// Decode failures are answered with the codec's error and `v1::validate` failures with `-32602`
    /// (Invalid params); otherwise the handler's `expected` is encoded as the result or error reply.
    /// @tparam Params  Parameter type to decode from `params`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Handler Callable `(Derived&, const Params&, OutboundSink&) -> expected<Result, RpcError>`.
    /// @param env     Parsed inbound request envelope.
    /// @param sink    Outbound byte sink.
    /// @param handler Invokes the `Derived` hook.
    /// @return Empty on success; `-32600` (Invalid Request) if the envelope has no `id`.
    template <typename Params, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_request(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
        if (!env.id.has_value()) {
            return unexpected(RpcError::invalid_request("Expected request id for " + std::string(env.method)));
        }

        auto params = Codec::template decode<Params>(env.params_json);
        if (!params) {
            emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
            return {};
        }
        if (auto reason = v1::validate(*params)) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
            return {};
        }

        auto result = handler(self(), *params, sink);
        if (!result) {
            emit_bytes(Codec::encode_error(*env.id, result.error()), sink);
        } else {
            emit_bytes(Codec::encode_result(*env.id, *result), sink);
        }
        return {};
    }
};

} // namespace buraq
// ---- end buraq/core/agent_base.hpp ----

// ---- begin buraq/core/client_base.hpp ----
#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// Root namespace of the buraq Agent Client Protocol (ACP) v1 SDK.
namespace buraq {

// CRTP base for ACP clients. Every handler is optional and detected at compile time; agent requests for
// methods the derived class does not implement are answered with -32601 (Method not found).
//
// Agent -> Client requests:      on_request_permission, on_fs_read_text_file, on_fs_write_text_file,
//                                on_terminal_{create,output,wait_for_exit,kill,release}, on_elicitation_create,
//                                on_ext_method
// Agent -> Client notifications: on_session_update (+ on_unhandled_session_update), on_elicitation_complete,
//                                on_cancel_request, on_ext_notification
// Responses without a typed callback: on_raw_response
/// CRTP base class for implementing an ACP v1 client over JSON-RPC 2.0.
///
/// `ClientBase` is sans-I/O: the caller owns the agent process and pipes, while `ClientBase` encodes outbound
/// requests, allocates integer request ids, correlates responses with typed callbacks and dispatches inbound
/// agent requests and notifications to optional `Derived` hooks. Hooks are detected at compile time with
/// `requires` expressions; agent requests for methods `Derived` does not implement are answered with
/// `-32601` (Method not found). Request hooks return `expected<Result, RpcError>`, encoded as the JSON-RPC
/// `result` or `error`.
///
/// Optional `Derived` hooks (Agent -> Client requests):
/// - `expected<v1::RequestPermissionResult, RpcError> on_request_permission(const v1::RequestPermissionParams&)`
///   -- `session/request_permission`
/// - `expected<v1::FsReadTextFileResult, RpcError> on_fs_read_text_file(const v1::FsReadTextFileParams&)` -- `fs/read_text_file`
/// - `expected<v1::FsWriteTextFileResult, RpcError> on_fs_write_text_file(const v1::FsWriteTextFileParams&)` -- `fs/write_text_file`
/// - `expected<v1::TerminalCreateResult, RpcError> on_terminal_create(const v1::TerminalCreateParams&)` -- `terminal/create`
/// - `expected<v1::TerminalOutputResult, RpcError> on_terminal_output(const v1::TerminalOutputParams&)` -- `terminal/output`
/// - `expected<v1::TerminalWaitForExitResult, RpcError> on_terminal_wait_for_exit(const v1::TerminalWaitForExitParams&)`
///   -- `terminal/wait_for_exit`
/// - `expected<v1::TerminalKillResult, RpcError> on_terminal_kill(const v1::TerminalKillParams&)` -- `terminal/kill`
/// - `expected<v1::TerminalReleaseResult, RpcError> on_terminal_release(const v1::TerminalReleaseParams&)` -- `terminal/release`
/// - `expected<v1::CreateElicitationResult, RpcError> on_elicitation_create(const v1::CreateElicitationParams&)`
///   -- `elicitation/create`
/// - `expected<RawJson, RpcError> on_ext_method(std::string_view method, const RawJson& params)` -- any `_`-prefixed request
///
/// Optional `Derived` hooks (Agent -> Client notifications and responses):
/// - `void on_session_update(const v1::SessionUpdateNotification&)` -- `session/update`
/// - `void on_unhandled_session_update(const RawEnvelope&, const RpcError&)` -- `session/update` payloads that fail
///   to decode (e.g. newer `sessionUpdate` kinds); without it feed() returns the decode error
/// - `void on_elicitation_complete(const v1::CompleteElicitationParams&)` -- `elicitation/complete`
/// - `void on_cancel_request(const v1::CancelRequestParams&)` -- `$/cancel_request`
/// - `void on_ext_notification(std::string_view method, const RawJson& params)` -- any `_`-prefixed notification
/// - `void on_raw_response(const RawEnvelope&)` -- responses with no pending typed callback
///
/// @code
/// class MyClient : public buraq::ClientBase<MyClient, buraq::NlohmannCodec> {
/// public:
///     void on_session_update(const buraq::v1::SessionUpdateNotification& n) {
///         if (auto* chunk = std::get_if<buraq::v1::AgentMessageChunk>(&n.update)) { /* render */ }
///     }
///
///     buraq::expected<buraq::v1::RequestPermissionResult, buraq::RpcError>
///     on_request_permission(const buraq::v1::RequestPermissionParams& p) {
///         return buraq::v1::RequestPermissionResult{
///             .outcome = buraq::v1::PermissionSelectedOutcome{.optionId = p.options.front().optionId}};
///     }
/// };
///
/// MyClient client;
/// auto to_agent = [&](std::span<const std::byte> bytes) { /* write bytes + '\n' to the agent's stdin */ };
///
/// client.send_initialize(
///     buraq::v1::InitializeParams{.clientCapabilities = {
///         .fs = buraq::v1::ClientCapabilities::FsCapabilities{.readTextFile = true}}},
///     to_agent,
///     [](buraq::expected<buraq::v1::InitializeResult, buraq::RpcError> res) { /* ... */ });
///
/// // For every NDJSON line read from the agent's stdout:
/// client.feed(std::as_bytes(std::span{line.data(), line.size()}), to_agent);
/// @endcode
///
/// @tparam Derived The concrete client type (CRTP); must publicly derive from `ClientBase<Derived, Codec>`.
/// @tparam Codec   JSON codec satisfying the JsonCodec concept (e.g. `GlazeCodec`, `NlohmannCodec`).
/// @note Not internally synchronized: the id counter and pending-request table must be accessed from one
///       thread at a time.
/// @see AgentBase
template <typename Derived, JsonCodec Codec>
class ClientBase {
protected:
    /// Returns `*this` downcast to the derived client type.
    /// @return Mutable reference to the `Derived` object.
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    /// Returns `*this` downcast to the derived client type (const overload).
    /// @return Const reference to the `Derived` object.
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    /// Type-erased continuation invoked with the response envelope of a pending request.
    using ResponseHandler = std::function<void(const RawEnvelope&)>;

    /// Next JSON-RPC request id to allocate; starts at 1 and increments per outbound request.
    std::int64_t next_id_{1};
    /// In-flight typed requests keyed by request id, awaiting their response.
    std::unordered_map<std::int64_t, ResponseHandler> pending_requests_{};

public:
    // --- Fire-and-ID Outbound Requests (for custom correlation via on_raw_response) ---
    /// Sends an ACP `initialize` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Protocol version, client capabilities and client info.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    /// @note Unlike the callback overload, this does not record agent_capabilities() / agent_info() / auth_methods().
    template <typename OutboundSink>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink) {
        return send_request_raw("initialize", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `authenticate` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Authentication method to use.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink) {
        return send_request_raw("authenticate", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `logout` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Logout parameters.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink) {
        return send_request_raw("logout", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/new` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Working directory and MCP servers for the new session.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink) {
        return send_request_raw("session/new", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/load` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session to load.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink) {
        return send_request_raw("session/load", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/resume` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session to resume.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/resume", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/list` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Listing filters / pagination.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink) {
        return send_request_raw("session/list", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/delete` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session to delete.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink) {
        return send_request_raw("session/delete", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/close` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session to close.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink) {
        return send_request_raw("session/close", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/set_mode` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session and mode id.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_mode", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/set_config_option` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session, option id and value.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_config_option", params, std::forward<OutboundSink>(sink));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    /// Sends an ACP `session/set_model` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session and model id.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    /// @note UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema.
    template <typename OutboundSink>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_model", params, std::forward<OutboundSink>(sink));
    }

    /// Sends an ACP `session/prompt` request without registering a callback.
    /// The id is allocated from an internal counter; the agent's reply is delivered to
    /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session id and prompt content blocks.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink) {
        return send_request_raw("session/prompt", params, std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    /// Sends a custom extension request without registering a callback.
    /// The reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        return send_request_raw(method, params, std::forward<OutboundSink>(sink));
    }

    // --- Typed Callback Outbound Requests (automatic decoding & ID correlation) ---
    // `initialize` responses are also recorded for agent_capabilities() / agent_info() / auth_methods()
    /// Sends an ACP `initialize` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::InitializeResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Protocol version, client capabilities and client info.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    /// @note On success the result is also recorded for agent_capabilities(), agent_info() and auth_methods() before @p cb runs.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::InitializeResult>(
            "initialize", params, std::forward<OutboundSink>(sink),
            [this, callback = std::forward<Callback>(cb)](expected<v1::InitializeResult, RpcError> res) mutable {
                if (res) {
                    agent_capabilities_ = res->agentCapabilities;
                    agent_info_ = res->agentInfo;
                    auth_methods_ = res->authMethods;
                }
                callback(std::move(res));
            });
    }

    /// Sends an ACP `authenticate` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::AuthenticateResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Authentication method to use.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::AuthenticateResult>(
            "authenticate", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `logout` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::LogoutResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Logout parameters.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::LogoutResult>(
            "logout", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/new` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionNewResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Working directory and MCP servers for the new session.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionNewResult>(
            "session/new", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/load` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionLoadResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session to load.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionLoadResult>(
            "session/load", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/resume` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionResumeResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session to resume.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionResumeResult>(
            "session/resume", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/list` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionListResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Listing filters / pagination.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionListResult>(
            "session/list", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/delete` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionDeleteResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session to delete.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionDeleteResult>(
            "session/delete", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/close` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionCloseResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session to close.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionCloseResult>(
            "session/close", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/set_mode` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetModeResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session and mode id.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModeResult>(
            "session/set_mode", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/set_config_option` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetConfigOptionResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session, option id and value.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params,
                                                OutboundSink&& sink,
                                                Callback&& cb) {
        return send_request_typed<v1::SessionSetConfigOptionResult>(
            "session/set_config_option", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    /// Sends an ACP `session/set_model` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetModelResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session and model id.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    /// @note UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModelResult>(
            "session/set_model", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends an ACP `session/prompt` request and invokes @p cb with the typed reply.
    /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
    /// with that id is fed in (or cancel_all_pending() is called).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionPromptResult, RpcError>)` with the decoded result or the error reply.
    /// @param params Session id and prompt content blocks.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionPromptResult>(
            "session/prompt", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    /// Sends a custom extension request and invokes @p cb with the raw JSON reply.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked once as `cb(expected<RawJson, RpcError>)` with the decoded result or the error reply.
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The JSON-RPC request id used.
    template <typename OutboundSink, typename Callback>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<RawJson>(method, params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // --- Outbound Notifications ---
    /// Sends a `session/cancel` notification to stop the ongoing prompt turn of a session.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Session whose prompt turn should be cancelled.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_session_cancel(const v1::SessionCancelParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/cancel", params), std::forward<OutboundSink>(sink));
    }

    // Ask the agent to abandon an in-flight request (`$/cancel_request`). The pending callback stays
    // registered: the agent still answers, typically with -32800 (Request cancelled).
    /// Sends a `$/cancel_request` notification for an in-flight request.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Id of the request to cancel.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    /// Sends a custom extension notification to the agent.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Negotiated Agent Capabilities (populated by the typed send_initialize overload) ---
    /// Returns the agent capabilities negotiated by `initialize`.
    /// @return The `agentCapabilities` of a successful typed send_initialize() reply, or `std::nullopt` before one.
    [[nodiscard]] const std::optional<v1::AgentCapabilities>& agent_capabilities() const noexcept {
        return agent_capabilities_;
    }

    /// Returns the agent implementation info from `initialize`.
    /// @return The `agentInfo` of a successful typed send_initialize() reply, or `std::nullopt` if absent or before one.
    [[nodiscard]] const std::optional<v1::ImplementationInfo>& agent_info() const noexcept {
        return agent_info_;
    }

    /// Returns the authentication methods advertised by the agent in `initialize`.
    /// @return The `authMethods` of a successful typed send_initialize() reply; empty before one.
    [[nodiscard]] const std::vector<v1::AuthMethod>& auth_methods() const noexcept {
        return auth_methods_;
    }

    /// Returns the number of typed requests still awaiting a response.
    /// @return Size of the pending-request table.
    [[nodiscard]] std::size_t pending_count() const noexcept {
        return pending_requests_.size();
    }

    // Fail and clear all in-flight requests (useful on transport disconnect)
    /// Fails and clears every in-flight typed request.
    /// Each pending callback is invoked with an error envelope carrying @p reason. Useful on transport disconnect.
    /// @param reason Error delivered to every pending callback; defaults to `-32800` (Request cancelled).
    void cancel_all_pending(const RpcError& reason = RpcError::request_cancelled()) {
        auto pending = std::move(pending_requests_);
        pending_requests_.clear();
        for (auto& [id, handler] : pending) {
            RawEnvelope err_env{.id = RequestId{id}, .error = reason};
            handler(err_env);
        }
    }

    // --- Inbound Frame Entry Point ---
    /// Processes one inbound JSON-RPC frame from the agent.
    /// Notifications go to dispatch_notification(), agent requests to dispatch_agent_request() (replies are
    /// written to @p sink) and responses to dispatch_response().
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param frame Bytes of a single JSON-RPC message (one NDJSON line, without the newline).
    /// @param sink  Outbound byte sink used to answer agent requests.
    /// @return Empty on success; an RpcError if the envelope cannot be parsed or classified (`-32600`), or if a
    ///         recognised notification fails to decode.
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) return unexpected(env.error());

        if (env->is_notification()) {
            return dispatch_notification(*env);
        }
        if (env->is_request()) {
            return dispatch_agent_request(*env, std::forward<OutboundSink>(sink));
        }
        if (env->is_response()) {
            return dispatch_response(*env);
        }
        return unexpected(RpcError::invalid_request());
    }

private:
    /// Agent capabilities from the last successful typed `initialize`.
    std::optional<v1::AgentCapabilities> agent_capabilities_{};
    /// Agent implementation info from the last successful typed `initialize`.
    std::optional<v1::ImplementationInfo> agent_info_{};
    /// Authentication methods from the last successful typed `initialize`.
    std::vector<v1::AuthMethod> auth_methods_{};

    /// Writes an already-encoded JSON-RPC message to the outbound sink as raw bytes.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param wire Serialized JSON-RPC message.
    /// @param sink Outbound byte sink.
    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

    /// Allocates a request id and emits a request without registering a callback.
    /// @tparam Params Parameter type to encode.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param method JSON-RPC method name.
    /// @param params Request parameters.
    /// @param sink   Outbound byte sink.
    /// @return The allocated request id.
    template <typename Params, typename OutboundSink>
    std::int64_t send_request_raw(std::string_view method, const Params& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
        return id;
    }

    /// Allocates a request id, registers a decoding continuation and emits the request.
    /// The continuation forwards an error reply as `unexpected`, otherwise decodes `result` as @p Result.
    /// @tparam Result   Expected result type of the reply.
    /// @tparam Params   Parameter type to encode.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Callback Callable invoked as `cb(expected<Result, RpcError>)`.
    /// @param method JSON-RPC method name.
    /// @param params Request parameters.
    /// @param sink   Outbound byte sink.
    /// @param cb     Completion callback.
    /// @return The allocated request id.
    template <typename Result, typename Params, typename OutboundSink, typename Callback>
    std::int64_t send_request_typed(std::string_view method,
                                    const Params& params,
                                    OutboundSink&& sink,
                                    Callback&& cb) {
        const auto id = next_id_++;
        pending_requests_.emplace(id, [callback = std::forward<Callback>(cb)](const RawEnvelope& env) mutable {
            if (env.error.has_value()) {
                callback(expected<Result, RpcError>(unexpected(*env.error)));
                return;
            }
            callback(Codec::template decode<Result>(env.result_json));
        });

        emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
        return id;
    }

    /// Dispatches an agent notification to the matching `Derived` hook.
    /// Handles `session/update`, `elicitation/complete`, `$/cancel_request` and `_`-prefixed extensions;
    /// other notifications, or ones without a matching hook, are ignored.
    /// @param env Parsed notification envelope.
    /// @return Empty on success, or the decode error of a recognised notification.
    expected<void, RpcError> dispatch_notification(const RawEnvelope& env) {
        if (env.method == "session/update") {
            if constexpr (requires(Derived& d, const v1::SessionUpdateNotification& u) {
                d.on_session_update(u);
            }) {
                auto update = Codec::template decode<v1::SessionUpdateNotification>(env.params_json);
                if (!update) {
                    // Forward-compat: newer agents may emit sessionUpdate kinds this SDK does not model yet
                    if constexpr (requires(Derived& d, const RawEnvelope& e, const RpcError& err) {
                        d.on_unhandled_session_update(e, err);
                    }) {
                        self().on_unhandled_session_update(env, update.error());
                        return {};
                    }
                    return unexpected(update.error());
                }
                self().on_session_update(*update);
            }
            return {};
        }

        if (env.method == "elicitation/complete") {
            if constexpr (requires(Derived& d, const v1::CompleteElicitationParams& p) {
                d.on_elicitation_complete(p);
            }) {
                auto params = Codec::template decode<v1::CompleteElicitationParams>(env.params_json);
                if (!params) return unexpected(params.error());
                self().on_elicitation_complete(*params);
            }
            return {};
        }

        if (env.method == "$/cancel_request") {
            if constexpr (requires(Derived& d, const v1::CancelRequestParams& p) { d.on_cancel_request(p); }) {
                auto params = Codec::template decode<v1::CancelRequestParams>(env.params_json);
                if (!params) return unexpected(params.error());
                self().on_cancel_request(*params);
            }
            return {};
        }

        if (!env.method.empty() && env.method.front() == '_') {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                d.on_ext_notification(m, p);
            }) {
                self().on_ext_notification(env.method, RawJson{std::string(env.params_json)});
            }
        }
        return {};
    }

    /// Decodes, validates and answers a typed agent request.
    /// Decode failures are answered with the codec's error and `v1::validate` failures with `-32602`
    /// (Invalid params); otherwise the handler's `expected` is encoded as the result or error reply.
    /// @tparam Params  Parameter type to decode from `params`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Handler Callable `(Derived&, const Params&) -> expected<Result, RpcError>`.
    /// @param env     Parsed request envelope (must carry an id).
    /// @param sink    Outbound byte sink.
    /// @param handler Invokes the `Derived` hook.
    /// @return Always empty.
    template <typename Params, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_agent_request(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
        auto params = Codec::template decode<Params>(env.params_json);
        if (!params) {
            emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
            return {};
        }
        if (auto reason = v1::validate(*params)) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
            return {};
        }

        auto res = handler(self(), *params);
        if (!res) {
            emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
        } else {
            emit_bytes(Codec::encode_result(*env.id, *res), sink);
        }
        return {};
    }

    /// Routes an agent request to the matching `Derived` hook by method name.
    /// Unimplemented methods are answered with `-32601` (Method not found).
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param env  Parsed request envelope.
    /// @param sink Outbound byte sink.
    /// @return Always empty.
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_agent_request(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.method == "session/request_permission") {
            if constexpr (requires(Derived& d, const v1::RequestPermissionParams& p) { d.on_request_permission(p); }) {
                return handle_agent_request<v1::RequestPermissionParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_request_permission(p); });
            }
        }

        if (env.method == "fs/read_text_file") {
            if constexpr (requires(Derived& d, const v1::FsReadTextFileParams& p) { d.on_fs_read_text_file(p); }) {
                return handle_agent_request<v1::FsReadTextFileParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_fs_read_text_file(p); });
            }
        }

        if (env.method == "fs/write_text_file") {
            if constexpr (requires(Derived& d, const v1::FsWriteTextFileParams& p) { d.on_fs_write_text_file(p); }) {
                return handle_agent_request<v1::FsWriteTextFileParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_fs_write_text_file(p); });
            }
        }

        if (env.method == "terminal/create") {
            if constexpr (requires(Derived& d, const v1::TerminalCreateParams& p) { d.on_terminal_create(p); }) {
                return handle_agent_request<v1::TerminalCreateParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_create(p); });
            }
        }

        if (env.method == "terminal/output") {
            if constexpr (requires(Derived& d, const v1::TerminalOutputParams& p) { d.on_terminal_output(p); }) {
                return handle_agent_request<v1::TerminalOutputParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_output(p); });
            }
        }

        if (env.method == "terminal/wait_for_exit") {
            if constexpr (requires(Derived& d, const v1::TerminalWaitForExitParams& p) {
                d.on_terminal_wait_for_exit(p);
            }) {
                return handle_agent_request<v1::TerminalWaitForExitParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_wait_for_exit(p); });
            }
        }

        if (env.method == "terminal/kill") {
            if constexpr (requires(Derived& d, const v1::TerminalKillParams& p) { d.on_terminal_kill(p); }) {
                return handle_agent_request<v1::TerminalKillParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_kill(p); });
            }
        }

        if (env.method == "terminal/release") {
            if constexpr (requires(Derived& d, const v1::TerminalReleaseParams& p) { d.on_terminal_release(p); }) {
                return handle_agent_request<v1::TerminalReleaseParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_release(p); });
            }
        }

        if (env.method == "elicitation/create") {
            if constexpr (requires(Derived& d, const v1::CreateElicitationParams& p) { d.on_elicitation_create(p); }) {
                return handle_agent_request<v1::CreateElicitationParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_elicitation_create(p); });
            }
        }

        if (!env.method.empty() && env.method.front() == '_') {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError>>;
            }) {
                auto res = self().on_ext_method(env.method, RawJson{std::string(env.params_json)});
                if (!res) {
                    emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
                } else {
                    emit_bytes(Codec::encode_result(*env.id, *res), sink);
                }
                return {};
            }
        }

        emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
        return {};
    }

    /// Completes a pending typed request, or forwards the response to `on_raw_response`.
    /// Only integer ids are looked up in the pending table; the handler is removed before it is invoked.
    /// @param env Parsed response envelope.
    /// @return Always empty.
    expected<void, RpcError> dispatch_response(const RawEnvelope& env) {
        if (env.id && std::holds_alternative<std::int64_t>(*env.id)) {
            const auto id = std::get<std::int64_t>(*env.id);
            if (auto it = pending_requests_.find(id); it != pending_requests_.end()) {
                auto handler = std::move(it->second);
                pending_requests_.erase(it);
                handler(env);
                return {};
            }
        }

        if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
            self().on_raw_response(env);
        }
        return {};
    }
};

} // namespace buraq
// ---- end buraq/core/client_base.hpp ----

// ---- begin buraq/acp.hpp ----
// include/buraq/acp.hpp
/// @file
/// @brief Umbrella header for the buraq ACP v1 SDK.
///
/// Includes the C++20 compatibility shims (detail/compat.hpp), the JSON-RPC 2.0
/// envelope types and codec concept (protocol/jsonrpc.hpp), the ACP v1 schema
/// (protocol/schema_v1.hpp) and the agent/client bases (core/agent_base.hpp,
/// core/client_base.hpp). JSON codecs (buraq/codecs/glaze.hpp,
/// buraq/codecs/nlohmann.hpp) and the stdio transport (buraq/transport/stdio.hpp)
/// are separate opt-in headers and must be included explicitly.
///
/// @namespace buraq
/// @brief Root namespace of the buraq header-only Agent Client Protocol (ACP) v1 SDK.
///
/// @namespace buraq::v1
/// @brief ACP protocol version 1 schema types (requests, responses, notifications).
///
/// @namespace buraq::detail
/// @brief Internal implementation helpers; not part of the stable public API.
///
/// @namespace buraq::transport
/// @brief Transports that move NDJSON-framed JSON-RPC messages between peers.
///
/// @namespace buraq::error_code
/// @brief JSON-RPC 2.0 and ACP v1 numeric error codes.
// ---- end buraq/acp.hpp ----

// ---- begin buraq/transport/stdio.hpp ----
/// @file
/// @brief NDJSON stdio transport: newline-delimited JSON-RPC frames over streams.
///
/// Opt-in header (not included by buraq/acp.hpp).

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace buraq::transport {

// Sink callable compatible with AgentBase::feed(..., sink)
/// Outbound sink writing each frame as one NDJSON line to a stream.
///
/// Callable compatible with `AgentBase::feed(..., sink)`.
struct StdoutSink {
  /// Destination stream (defaults to std::cout).
  std::ostream& out{std::cout};

  /// Writes @p bytes, appends a newline if not already newline-terminated, and flushes.
  /// @param bytes Encoded frame.
  void operator()(std::span<const std::byte> bytes) const {
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (bytes.empty() || bytes.back() != std::byte{'\n'}) {
      out.put('\n');
    }
    out.flush();
  }
};

// Blocking line loop for standalone CLI ACP agents
template <typename Agent>
/// Blocking NDJSON read loop for standalone CLI ACP agents.
///
/// Disables stdio synchronization, unties @p in, then reads lines until EOF.
/// Empty lines are skipped and a trailing `\r` is stripped; each line is passed to
/// `agent.feed()` with a StdoutSink on @p out. Frame errors are logged to std::cerr.
/// @tparam Agent Type providing `feed(std::span<const std::byte>, Sink&&)`.
/// @param agent The agent to feed.
/// @param in Input stream (defaults to std::cin).
/// @param out Output stream (defaults to std::cout).
/// @return 0 when input is exhausted.
int run_stdio_agent(Agent& agent, std::istream& in = std::cin, std::ostream& out = std::cout) {
  // Untie cin/cout and disable stdio sync for fast pipe throughput
  std::ios::sync_with_stdio(false);
  in.tie(nullptr);

  StdoutSink sink{out};
  std::string line;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    auto bytes = std::as_bytes(std::span{line.data(), line.size()});
    auto status = agent.feed(bytes, sink);
    if (!status) {
      std::cerr << "[buraq] frame error: " << status.error().message << '\n';
    }
  }
  return 0;
}

} // namespace buraq::transport
// ---- end buraq/transport/stdio.hpp ----
