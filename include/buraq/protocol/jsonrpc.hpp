#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include "buraq/detail/compat.hpp"

namespace buraq {

using RequestId = std::variant<std::int64_t, std::string>;

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