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
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include "buraq/detail/compat.hpp"

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