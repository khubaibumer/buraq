#pragma once

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
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

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
template <typename Derived, JsonCodec Codec>
class ClientBase {
protected:
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    using ResponseHandler = std::function<void(const RawEnvelope&)>;

    std::int64_t next_id_{1};
    std::unordered_map<std::int64_t, ResponseHandler> pending_requests_{};

public:
    // --- Fire-and-ID Outbound Requests (for custom correlation via on_raw_response) ---
    template <typename OutboundSink>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink) {
        return send_request_raw("initialize", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink) {
        return send_request_raw("authenticate", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink) {
        return send_request_raw("logout", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink) {
        return send_request_raw("session/new", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink) {
        return send_request_raw("session/load", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/resume", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink) {
        return send_request_raw("session/list", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink) {
        return send_request_raw("session/delete", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink) {
        return send_request_raw("session/close", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_mode", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_config_option", params, std::forward<OutboundSink>(sink));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    template <typename OutboundSink>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_model", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink) {
        return send_request_raw("session/prompt", params, std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    template <typename OutboundSink>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        return send_request_raw(method, params, std::forward<OutboundSink>(sink));
    }

    // --- Typed Callback Outbound Requests (automatic decoding & ID correlation) ---
    // `initialize` responses are also recorded for agent_capabilities() / agent_info() / auth_methods()
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

    template <typename OutboundSink, typename Callback>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::AuthenticateResult>(
            "authenticate", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::LogoutResult>(
            "logout", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionNewResult>(
            "session/new", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionLoadResult>(
            "session/load", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionResumeResult>(
            "session/resume", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionListResult>(
            "session/list", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionDeleteResult>(
            "session/delete", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionCloseResult>(
            "session/close", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModeResult>(
            "session/set_mode", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params,
                                                OutboundSink&& sink,
                                                Callback&& cb) {
        return send_request_typed<v1::SessionSetConfigOptionResult>(
            "session/set_config_option", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModelResult>(
            "session/set_model", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionPromptResult>(
            "session/prompt", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<RawJson>(method, params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // --- Outbound Notifications ---
    template <typename OutboundSink>
    void send_session_cancel(const v1::SessionCancelParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/cancel", params), std::forward<OutboundSink>(sink));
    }

    // Ask the agent to abandon an in-flight request (`$/cancel_request`). The pending callback stays
    // registered: the agent still answers, typically with -32800 (Request cancelled).
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Negotiated Agent Capabilities (populated by the typed send_initialize overload) ---
    [[nodiscard]] const std::optional<v1::AgentCapabilities>& agent_capabilities() const noexcept {
        return agent_capabilities_;
    }

    [[nodiscard]] const std::optional<v1::ImplementationInfo>& agent_info() const noexcept {
        return agent_info_;
    }

    [[nodiscard]] const std::vector<v1::AuthMethod>& auth_methods() const noexcept {
        return auth_methods_;
    }

    [[nodiscard]] std::size_t pending_count() const noexcept {
        return pending_requests_.size();
    }

    // Fail and clear all in-flight requests (useful on transport disconnect)
    void cancel_all_pending(const RpcError& reason = RpcError::request_cancelled()) {
        auto pending = std::move(pending_requests_);
        pending_requests_.clear();
        for (auto& [id, handler] : pending) {
            RawEnvelope err_env{.id = RequestId{id}, .error = reason};
            handler(err_env);
        }
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
    std::optional<v1::AgentCapabilities> agent_capabilities_{};
    std::optional<v1::ImplementationInfo> agent_info_{};
    std::vector<v1::AuthMethod> auth_methods_{};

    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

    template <typename Params, typename OutboundSink>
    std::int64_t send_request_raw(std::string_view method, const Params& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
        return id;
    }

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
