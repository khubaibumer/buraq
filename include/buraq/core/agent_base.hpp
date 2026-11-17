#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

namespace buraq {
    template<typename Derived, JsonCodec Codec>
    class AgentBase {
    protected:
        constexpr Derived &self() noexcept { return static_cast<Derived &>(*this); }
        constexpr const Derived &self() const noexcept { return static_cast<const Derived &>(*this); }

        template<typename OutboundSink>
        static void emit_bytes(std::string_view wire, OutboundSink &&sink) {
            sink(std::as_bytes(std::span{wire.data(), wire.size()}));
        }

    public:
        template<typename OutboundSink>
        expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink &&sink) {
            auto env = Codec::parse_envelope(frame);
            if (!env) {
                return unexpected(env.error());
            }
            return dispatch_message(*env, std::forward<OutboundSink>(sink));
        }

        // --- Streaming Notifications (Agent -> Client) ---
        template<typename OutboundSink>
        void emit_session_update(const v1::SessionUpdateNotification &update, OutboundSink &&sink) {
            emit_bytes(Codec::encode_notification("session/update", update), std::forward<OutboundSink>(sink));
        }

        // --- Delegated Client Requests (Agent -> Client) ---
        template<typename OutboundSink>
        void request_permission(const RequestId &id,
                                const v1::RequestPermissionParams &params,
                                OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "session/request_permission", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_fs_read(const RequestId &id,
                             const v1::FsReadTextFileParams &params,
                             OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "fs/read_text_file", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_fs_write(const RequestId &id,
                              const v1::FsWriteTextFileParams &params,
                              OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "fs/write_text_file", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_terminal_create(const RequestId &id,
                                     const v1::TerminalCreateParams &params,
                                     OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "terminal/create", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_terminal_output(const RequestId &id,
                                     const v1::TerminalOutputParams &params,
                                     OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "terminal/output", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_terminal_wait_for_exit(const RequestId &id,
                                            const v1::TerminalWaitForExitParams &params,
                                            OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "terminal/wait_for_exit", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_terminal_kill(const RequestId &id,
                                   const v1::TerminalKillParams &params,
                                   OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "terminal/kill", params),
                       std::forward<OutboundSink>(sink));
        }

        template<typename OutboundSink>
        void request_terminal_release(const RequestId &id,
                                      const v1::TerminalReleaseParams &params,
                                      OutboundSink &&sink) {
            emit_bytes(Codec::encode_request(id, "terminal/release", params),
                       std::forward<OutboundSink>(sink));
        }

    private:
        template<typename OutboundSink>
        expected<void, RpcError> dispatch_message(const RawEnvelope &env, OutboundSink &&sink) {
            if (env.is_response()) {
                if constexpr (requires(Derived &d, const RawEnvelope &r) { d.on_raw_response(r); }) {
                    self().on_raw_response(env);
                }
                return {};
            }

            if (env.method == "initialize") {
                if constexpr (requires(Derived &d, const v1::InitializeParams &p) { d.on_initialize(p); }) {
                    return handle_request<v1::InitializeParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) { return d.on_initialize(p); });
                }
            }

            if (env.method == "session/new") {
                if constexpr (requires(Derived &d, const v1::SessionNewParams &p) { d.on_session_new(p); }) {
                    return handle_request<v1::SessionNewParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) { return d.on_session_new(p); });
                }
            }

            if (env.method == "session/load") {
                if constexpr (requires(Derived &d, const v1::SessionLoadParams &p) { d.on_session_load(p); }) {
                    return handle_request<v1::SessionLoadParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) { return d.on_session_load(p); });
                }
            }

            if (env.method == "session/set_mode") {
                if constexpr (requires(Derived &d, const v1::SessionSetModeParams &p) { d.on_session_set_mode(p); }) {
                    return handle_request<v1::SessionSetModeParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) { return d.on_session_set_mode(p); });
                }
            }

            if (env.method == "session/set_model") {
                if constexpr (requires(Derived &d, const v1::SessionSetModelParams &p) { d.on_session_set_model(p); }) {
                    return handle_request<v1::SessionSetModelParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) { return d.on_session_set_model(p); });
                }
            }

            if (env.method == "session/prompt") {
#if BURAQ_HAS_STD_GENERATOR
                if constexpr (requires(Derived &d, const v1::SessionPromptParams &p) {
                    { d.on_session_prompt_stream(p) } -> std::same_as<generator<v1::SessionUpdatePayload>>;
                }) {
                    return handle_request<v1::SessionPromptParams>(
                        env, sink, [](Derived &d, const auto &p, OutboundSink &s) {
                            for (auto &&update : d.on_session_prompt_stream(p)) {
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
                if constexpr (requires(Derived &d, const v1::SessionPromptParams &p, OutboundSink &s) {
                    d.on_session_prompt(p, s);
                }) {
                    return handle_request<v1::SessionPromptParams>(
                        env, sink, [](Derived &d, const auto &p, OutboundSink &s) {
                            return d.on_session_prompt(p, s);
                        });
                }
                if constexpr (requires(Derived &d, const v1::SessionPromptParams &p) { d.on_session_prompt(p); }) {
                    return handle_request<v1::SessionPromptParams>(
                        env, sink, [](Derived &d, const auto &p, auto &) {
                            return d.on_session_prompt(p);
                        });
                }
            }

            if (env.method == "session/cancel") {
                if constexpr (requires(Derived &d, const v1::SessionCancelParams &p) { d.on_session_cancel(p); }) {
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

        template<typename Params, typename OutboundSink, typename Handler>
        expected<void, RpcError> handle_request(const RawEnvelope &env, OutboundSink &sink, Handler &&handler) {
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