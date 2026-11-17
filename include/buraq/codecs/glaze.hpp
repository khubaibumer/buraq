#pragma once

#include <array>
#include <glaze/glaze.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

// Tagged-union discriminators required by ACP v1 JSON Schema
template <>
struct glz::meta<buraq::v1::ContentBlock> {
    static constexpr std::string_view tag = "type";
    static constexpr auto ids = std::array{"text", "resource_link"};
};

template <>
struct glz::meta<buraq::v1::SessionUpdatePayload> {
    static constexpr std::string_view tag = "sessionUpdate";
    static constexpr auto ids = std::array{"agent_message_chunk", "tool_call"};
};

namespace buraq {

namespace detail {

struct GlzRpcErrorWire {
    int code{-32603};
    std::string message;
    std::optional<glz::raw_json> data{};
};

struct GlzInboundEnvelope {
    std::string_view jsonrpc{};
    std::optional<RequestId> id{};
    std::string_view method{};
    glz::raw_json_view params{};
    glz::raw_json_view result{};
    std::optional<GlzRpcErrorWire> error{};
};

template <typename T>
struct GlzRequestWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    std::string_view method;
    const T* params{nullptr};
};

template <typename T>
struct GlzNotificationWire {
    std::string_view jsonrpc{"2.0"};
    std::string_view method;
    const T* params{nullptr};
};

template <typename T>
struct GlzResultWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    const T* result{nullptr};
};

struct GlzErrorResponseWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    GlzRpcErrorWire error;
};

} // namespace detail

struct GlazeCodec {
    static constexpr glz::opts kWriteOpts{.skip_null_members = true};
    static constexpr glz::opts kReadOpts{.error_on_unknown_keys = false};

    static expected<RawEnvelope, RpcError> parse_envelope(std::span<const std::byte> frame) {
        std::string_view sv{reinterpret_cast<const char*>(frame.data()), frame.size()};

        detail::GlzInboundEnvelope wire{};
        if (auto ec = glz::read<kReadOpts>(wire, sv); ec) {
            return unexpected(RpcError::parse_error(glz::format_error(ec, sv)));
        }

        if (wire.jsonrpc != "2.0") {
            return unexpected(RpcError::invalid_request("Missing or invalid jsonrpc: \"2.0\""));
        }

        RawEnvelope out{
            .id = std::move(wire.id),
            .method = wire.method,
            .params_json = wire.params.str,
            .result_json = wire.result.str,
        };

        if (wire.error) {
            std::optional<std::string> data_str{};
            if (wire.error->data && !wire.error->data->str.empty()) {
                data_str = std::move(wire.error->data->str);
            }
            out.error = RpcError{
                .code = wire.error->code,
                .message = std::move(wire.error->message),
                .data_json = std::move(data_str),
            };
        }

        return out;
    }

    template <typename T>
    static expected<T, RpcError> decode(std::string_view json) {
        if (json.empty()) {
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            }
            return unexpected(RpcError::invalid_params("Empty JSON payload"));
        }

        T value{};
        if (auto ec = glz::read<kReadOpts>(value, json); ec) {
            return unexpected(RpcError::invalid_params(glz::format_error(ec, json)));
        }
        return value;
    }

    template <typename T>
    static std::string encode_request(const RequestId& id, std::string_view method, const T& val) {
        std::string out;
        detail::GlzRequestWire<T> wire{.id = id, .method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    template <typename T>
    static std::string encode_notification(std::string_view method, const T& val) {
        std::string out;
        detail::GlzNotificationWire<T> wire{.method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    template <typename T>
    static std::string encode_result(const RequestId& id, const T& val) {
        std::string out;
        detail::GlzResultWire<T> wire{.id = id, .result = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    static std::string encode_error(const RequestId& id, const RpcError& err) {
        std::string out;
        std::optional<glz::raw_json> data{};
        if (err.data_json && !err.data_json->empty()) {
            data = glz::raw_json{*err.data_json};
        }
        detail::GlzErrorResponseWire wire{
            .id = id,
            .error = detail::GlzRpcErrorWire{
                .code = err.code,
                .message = err.message,
                .data = std::move(data),
            },
        };
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }
};

static_assert(JsonCodec<GlazeCodec>);

} // namespace buraq