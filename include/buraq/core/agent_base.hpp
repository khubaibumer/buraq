#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

namespace buraq {

template <typename Derived, JsonCodec Codec>
class AgentBase {
protected:
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

public:
    // Sans-I/O entry point: feed raw bytes from any transport (stdio, WebSocket, ring buffer)
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) {
            return unexpected(env.error());
        }
        return dispatch_message(*env, std::forward<OutboundSink>(sink));
    }

    // Helper for derived agents to emit streaming session/update notifications
    template <typename OutboundSink>
    void emit_session_update(const v1::SessionUpdateNotification& update, OutboundSink&& sink) {
        auto wire = Codec::encode_notification("session/update", update);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
    }

    // Helper for derived agents to invoke delegated client tools (e.g., fs/read_text_file)
    template <typename OutboundSink>
    void request_fs_read(const RequestId& id,
                         const v1::FsReadTextFileParams& params,
                         OutboundSink&& sink) {
        auto wire = Codec::encode_request(id, "fs/read_text_file", params);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
    }

private:
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_message(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.is_response()) {
            if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
                self().on_raw_response(env);
            }
            return {};
        }

        if (env.method == "initialize") {
            return handle_request<v1::InitializeParams>(env, sink, [](Derived& d, const auto& p, auto&) {
                if constexpr (requires { d.on_initialize(p); }) {
                    return d.on_initialize(p);
                } else {
                    return expected<v1::InitializeResult, RpcError>(
                        unexpected(RpcError::method_not_found("initialize")));
                }
            });
        }

        if (env.method == "session/new") {
            return handle_request<v1::SessionNewParams>(env, sink, [](Derived& d, const auto& p, auto&) {
                if constexpr (requires { d.on_session_new(p); }) {
                    return d.on_session_new(p);
                } else {
                    return expected<v1::SessionNewResult, RpcError>(
                        unexpected(RpcError::method_not_found("session/new")));
                }
            });
        }

        if (env.method == "session/prompt") {
            return handle_request<v1::SessionPromptParams>(env, sink, [](Derived& d, const auto& p, OutboundSink& s) {
                if constexpr (requires { d.on_session_prompt(p, s); }) {
                    return d.on_session_prompt(p, s);
                } else if constexpr (requires { d.on_session_prompt(p); }) {
                    return d.on_session_prompt(p);
                } else {
                    return expected<v1::SessionPromptResult, RpcError>(
                        unexpected(RpcError::method_not_found("session/prompt")));
                }
            });
        }

        if (env.method == "session/cancel") {
            if constexpr (requires(Derived& d, const v1::SessionCancelParams& p) { d.on_session_cancel(p); }) {
                if (auto params = Codec::template decode<v1::SessionCancelParams>(env.params_json)) {
                    self().on_session_cancel(*params);
                }
            }
            return {};
        }

        if (env.id.has_value()) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
        }
        return {};
    }

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