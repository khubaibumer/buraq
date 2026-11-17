#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

namespace buraq {

template <typename Derived, JsonCodec Codec>
class ClientBase {
protected:
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    std::int64_t next_id_{1};

public:
    // Send typed ACP requests to the agent
    template <typename OutboundSink>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        auto wire = Codec::encode_request(RequestId{id}, "initialize", params);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        auto wire = Codec::encode_request(RequestId{id}, "session/new", params);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        auto wire = Codec::encode_request(RequestId{id}, "session/prompt", params);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    void send_session_cancel(const v1::SessionCancelParams& params, OutboundSink&& sink) {
        auto wire = Codec::encode_notification("session/cancel", params);
        emit_bytes(wire, std::forward<OutboundSink>(sink));
    }

    // Feed raw incoming frames from the agent subprocess/socket
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
    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

    expected<void, RpcError> dispatch_notification(const RawEnvelope& env) {
        if (env.method == "session/update") {
            if constexpr (requires(Derived& d, const v1::SessionUpdateNotification& u) {
                d.on_session_update(u);
            }) {
                auto update = Codec::template decode<v1::SessionUpdateNotification>(env.params_json);
                if (!update) return unexpected(update.error());
                self().on_session_update(*update);
            }
        }
        return {};
    }

    template <typename OutboundSink>
    expected<void, RpcError> dispatch_agent_request(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.method == "fs/read_text_file") {
            if constexpr (requires(Derived& d, const v1::FsReadTextFileParams& p) {
                d.on_fs_read_text_file(p);
            }) {
                auto params = Codec::template decode<v1::FsReadTextFileParams>(env.params_json);
                if (!params) {
                    emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
                    return {};
                }
                auto res = self().on_fs_read_text_file(*params);
                if (!res) {
                    emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
                } else {
                    emit_bytes(Codec::encode_result(*env.id, *res), sink);
                }
                return {};
            }
        }

        if (env.method == "fs/write_text_file") {
            if constexpr (requires(Derived& d, const v1::FsWriteTextFileParams& p) {
                d.on_fs_write_text_file(p);
            }) {
                auto params = Codec::template decode<v1::FsWriteTextFileParams>(env.params_json);
                if (!params) {
                    emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
                    return {};
                }
                auto res = self().on_fs_write_text_file(*params);
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

    expected<void, RpcError> dispatch_response(const RawEnvelope& env) {
        if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
            self().on_raw_response(env);
        }
        return {};
    }
};

} // namespace buraq