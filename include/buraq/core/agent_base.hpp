#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

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
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) {
            return unexpected(env.error());
        }
        return dispatch_message(*env, std::forward<OutboundSink>(sink));
    }

    // True once `initialize` has completed successfully on this connection
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    // --- Negotiated Client Capabilities (populated by `initialize`) ---
    [[nodiscard]] const std::optional<v1::ClientCapabilities>& client_capabilities() const noexcept {
        return client_capabilities_;
    }

    [[nodiscard]] const std::optional<v1::ImplementationInfo>& client_info() const noexcept {
        return client_info_;
    }

    [[nodiscard]] bool client_can_read_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->readTextFile;
    }

    [[nodiscard]] bool client_can_write_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->writeTextFile;
    }

    [[nodiscard]] bool client_supports_terminal() const noexcept {
        return client_capabilities_ && client_capabilities_->terminal;
    }

    [[nodiscard]] bool client_supports_elicitation_form() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->form;
    }

    [[nodiscard]] bool client_supports_elicitation_url() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->url;
    }

    [[nodiscard]] bool client_supports_terminal_auth() const noexcept {
        return client_capabilities_ && client_capabilities_->auth && client_capabilities_->auth->terminal;
    }

    [[nodiscard]] bool client_supports_boolean_config_options() const noexcept {
        return client_capabilities_ && client_capabilities_->session &&
               client_capabilities_->session->configOptions && client_capabilities_->session->configOptions->boolean;
    }

    // --- Notifications (Agent -> Client) ---
    template <typename OutboundSink>
    void emit_session_update(const v1::SessionUpdateNotification& update, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/update", update), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void notify_elicitation_complete(const v1::CompleteElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("elicitation/complete", params), std::forward<OutboundSink>(sink));
    }

    // Ask the client to abandon a request this agent sent earlier (`$/cancel_request`)
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension notification
    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Delegated Client Requests (Agent -> Client); replies arrive via on_raw_response ---
    template <typename OutboundSink>
    void request_permission(const RequestId& id, const v1::RequestPermissionParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "session/request_permission", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_fs_read(const RequestId& id, const v1::FsReadTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/read_text_file", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_fs_write(const RequestId& id, const v1::FsWriteTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/write_text_file", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_create(const RequestId& id, const v1::TerminalCreateParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/create", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_output(const RequestId& id, const v1::TerminalOutputParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/output", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_wait_for_exit(const RequestId& id,
                                        const v1::TerminalWaitForExitParams& params,
                                        OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/wait_for_exit", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_kill(const RequestId& id, const v1::TerminalKillParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/kill", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_release(const RequestId& id, const v1::TerminalReleaseParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/release", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_elicitation(const RequestId& id, const v1::CreateElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "elicitation/create", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    template <typename OutboundSink>
    void request_ext_method(const RequestId& id, std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, method, params), std::forward<OutboundSink>(sink));
    }

private:
    bool initialized_{false};
    std::optional<v1::ClientCapabilities> client_capabilities_{};
    std::optional<v1::ImplementationInfo> client_info_{};

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
