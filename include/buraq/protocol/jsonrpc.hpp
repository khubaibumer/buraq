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

using RequestId = std::variant<std::int64_t, std::string>;

// Arbitrary, codec-neutral JSON value kept as serialized text (e.g. `rawInput`, elicitation schemas,
// extension-method payloads). An empty string encodes as JSON `null`.
struct RawJson {
    std::string json{};
    auto operator<=>(const RawJson&) const = default;
};

namespace detail {

// Serialize `text` as a JSON string literal (RFC 8259 escaping)
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
inline constexpr int parse_error = -32700;
inline constexpr int invalid_request = -32600;
inline constexpr int method_not_found = -32601;
inline constexpr int invalid_params = -32602;
inline constexpr int internal_error = -32603;
inline constexpr int request_cancelled = -32800;
inline constexpr int auth_required = -32000;
inline constexpr int resource_not_found = -32002;
} // namespace error_code

struct RpcError {
    int code{-32603};
    std::string message;
    std::optional<std::string> data_json{};

    static RpcError parse_error(std::string_view msg = "Parse error") {
        return {-32700, std::string(msg), std::nullopt};
    }
    static RpcError invalid_request(std::string_view msg = "Invalid Request") {
        return {-32600, std::string(msg), std::nullopt};
    }
    static RpcError method_not_found(std::string_view method) {
        return {-32601, "Method not found: " + std::string(method), std::nullopt};
    }
    static RpcError invalid_params(std::string_view msg = "Invalid params") {
        return {-32602, std::string(msg), std::nullopt};
    }
    static RpcError internal_error(std::string_view msg = "Internal error") {
        return {-32603, std::string(msg), std::nullopt};
    }
    static RpcError request_cancelled() {
        return {-32800, "Request cancelled", std::nullopt};
    }
    static RpcError auth_required(std::string_view msg = "Authentication required") {
        return {-32000, std::string(msg), std::nullopt};
    }
    static RpcError resource_not_found(std::string_view msg = "Resource not found",
                                       std::optional<std::string> data_json = std::nullopt) {
        return {-32002, std::string(msg), std::move(data_json)};
    }
    // -32002 with `data.sessionId`, as recommended for unknown or closed sessions
    static RpcError session_not_found(std::string_view session_id) {
        return resource_not_found("Session not found: " + std::string(session_id),
                                  "{\"sessionId\":" + detail::json_quote(session_id) + "}");
    }
};

// Raw view over a decoded JSON-RPC envelope (params/result kept as raw JSON slices)
struct RawEnvelope {
    std::optional<RequestId> id{};
    std::string_view method{};
    std::string_view params_json{};
    std::string_view result_json{};
    std::optional<RpcError> error{};

    [[nodiscard]] bool is_request() const noexcept {
        return id.has_value() && !method.empty();
    }
    [[nodiscard]] bool is_notification() const noexcept {
        return !id.has_value() && !method.empty();
    }
    [[nodiscard]] bool is_response() const noexcept {
        return id.has_value() && method.empty();
    }
};

// Pluggable zero-overhead JSON codec contract
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