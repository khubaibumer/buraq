#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
    // --- Outbound Client -> Agent Requests & Notifications ---
    template <typename OutboundSink>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "initialize", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "session/new", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "session/load", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "session/set_mode", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "session/set_model", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, "session/prompt", params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename OutboundSink>
    void send_session_cancel(const v1::SessionCancelParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/cancel", params), std::forward<OutboundSink>(sink));
    }

    // --- Inbound Frame Entry Point ---
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

    template <typename Params, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_agent_request(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
        auto params = Codec::template decode<Params>(env.params_json);
        if (!params) {
            emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
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