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
#include <type_traits>
#include "buraq/core/async.hpp"
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

/// Root namespace of the buraq Agent Client Protocol (ACP) v1 SDK.
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
    /// CRTP base class for implementing an ACP v1 client over JSON-RPC 2.0.
    ///
    /// `ClientBase` is sans-I/O: the caller owns the agent process and pipes, while `ClientBase` encodes outbound
    /// requests, allocates integer request ids, correlates responses with typed callbacks and dispatches inbound
    /// agent requests and notifications to optional `Derived` hooks. Hooks are detected at compile time with
    /// `requires` expressions; agent requests for methods `Derived` does not implement are answered with
    /// `-32601` (Method not found). Request hooks return `expected<Result, RpcError>`, encoded as the JSON-RPC
    /// `result` or `error`.
    ///
    /// Optional `Derived` hooks (Agent -> Client requests):
    /// - `expected<v1::RequestPermissionResult, RpcError> on_request_permission(const v1::RequestPermissionParams&)`
    ///   -- `session/request_permission`
    /// - `expected<v1::FsReadTextFileResult, RpcError> on_fs_read_text_file(const v1::FsReadTextFileParams&)` -- `fs/read_text_file`
    /// - `expected<v1::FsWriteTextFileResult, RpcError> on_fs_write_text_file(const v1::FsWriteTextFileParams&)` -- `fs/write_text_file`
    /// - `expected<v1::TerminalCreateResult, RpcError> on_terminal_create(const v1::TerminalCreateParams&)` -- `terminal/create`
    /// - `expected<v1::TerminalOutputResult, RpcError> on_terminal_output(const v1::TerminalOutputParams&)` -- `terminal/output`
    /// - `expected<v1::TerminalWaitForExitResult, RpcError> on_terminal_wait_for_exit(const v1::TerminalWaitForExitParams&)`
    ///   -- `terminal/wait_for_exit`
    /// - `expected<v1::TerminalKillResult, RpcError> on_terminal_kill(const v1::TerminalKillParams&)` -- `terminal/kill`
    /// - `expected<v1::TerminalReleaseResult, RpcError> on_terminal_release(const v1::TerminalReleaseParams&)` -- `terminal/release`
    /// - `expected<v1::CreateElicitationResult, RpcError> on_elicitation_create(const v1::CreateElicitationParams&)`
    ///   -- `elicitation/create`
    /// - `expected<RawJson, RpcError> on_ext_method(std::string_view method, const RawJson& params)` -- any `_`-prefixed request
    ///
    /// Optional `Derived` hooks (Agent -> Client notifications and responses):
    /// - `void on_session_update(const v1::SessionUpdateNotification&)` -- `session/update`
    /// - `void on_unhandled_session_update(const RawEnvelope&, const RpcError&)` -- `session/update` payloads that fail
    ///   to decode (e.g. newer `sessionUpdate` kinds); without it feed() returns the decode error
    /// - `void on_elicitation_complete(const v1::CompleteElicitationParams&)` -- `elicitation/complete`
    /// - `void on_cancel_request(const v1::CancelRequestParams&)` -- `$/cancel_request`
    /// - `void on_ext_notification(std::string_view method, const RawJson& params)` -- any `_`-prefixed notification
    /// - `void on_raw_response(const RawEnvelope&)` -- responses with no pending typed callback
    ///
    /// @code
    /// class MyClient : public buraq::ClientBase<MyClient, buraq::NlohmannCodec> {
    /// public:
    ///     void on_session_update(const buraq::v1::SessionUpdateNotification& n) {
    ///         if (auto* chunk = std::get_if<buraq::v1::AgentMessageChunk>(&n.update)) { /* render */ }
    ///     }
    ///
    ///     buraq::expected<buraq::v1::RequestPermissionResult, buraq::RpcError>
    ///     on_request_permission(const buraq::v1::RequestPermissionParams& p) {
    ///         return buraq::v1::RequestPermissionResult{
    ///             .outcome = buraq::v1::PermissionSelectedOutcome{.optionId = p.options.front().optionId}};
    ///     }
    /// };
    ///
    /// MyClient client;
    /// auto to_agent = [&](std::span<const std::byte> bytes) { /* write bytes + '\n' to the agent's stdin */ };
    ///
    /// client.send_initialize(
    ///     buraq::v1::InitializeParams{.clientCapabilities = {
    ///         .fs = buraq::v1::ClientCapabilities::FsCapabilities{.readTextFile = true}}},
    ///     to_agent,
    ///     [](buraq::expected<buraq::v1::InitializeResult, buraq::RpcError> res) { /* ... */ });
    ///
    /// // For every NDJSON line read from the agent's stdout:
    /// client.feed(std::as_bytes(std::span{line.data(), line.size()}), to_agent);
    /// @endcode
    ///
    /// @tparam Derived The concrete client type (CRTP); must publicly derive from `ClientBase<Derived, Codec>`.
    /// @tparam Codec   JSON codec satisfying the JsonCodec concept (e.g. `GlazeCodec`, `NlohmannCodec`).
    /// @note Not internally synchronized: the id counter and pending-request table must be accessed from one
    ///       thread at a time.
    /// @see AgentBase
    template<typename Derived, JsonCodec Codec>
    class ClientBase {
    public:
        /// JSON codec this client encodes and decodes with.
        using codec_type = Codec;

    protected:
        /// Returns `*this` downcast to the derived client type.
        /// @return Mutable reference to the `Derived` object.
        constexpr Derived &self() noexcept { return static_cast<Derived &>(*this); }
        /// Returns `*this` downcast to the derived client type (const overload).
        /// @return Const reference to the `Derived` object.
        constexpr const Derived &self() const noexcept { return static_cast<const Derived &>(*this); }

        /// In-flight typed requests keyed by request id (thread-safe); ids start at 1.
        detail::PendingRequests pending_{};

    public:
        // --- Fire-and-ID Outbound Requests (for custom correlation via on_raw_response) ---
        /// Sends an ACP `initialize` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Protocol version, client capabilities and client info.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        /// @note Unlike the callback overload, this does not record agent_capabilities() / agent_info() / auth_methods().
        template<typename OutboundSink>
        std::int64_t send_initialize(const v1::InitializeParams &params, OutboundSink &&sink) {
            return send_request_raw("initialize", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `authenticate` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Authentication method to use.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_authenticate(const v1::AuthenticateParams &params, OutboundSink &&sink) {
            return send_request_raw("authenticate", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `logout` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Logout parameters.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_logout(const v1::LogoutParams &params, OutboundSink &&sink) {
            return send_request_raw("logout", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/new` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Working directory and MCP servers for the new session.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_new(const v1::SessionNewParams &params, OutboundSink &&sink) {
            return send_request_raw("session/new", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/load` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session to load.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_load(const v1::SessionLoadParams &params, OutboundSink &&sink) {
            return send_request_raw("session/load", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/resume` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session to resume.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_resume(const v1::SessionResumeParams &params, OutboundSink &&sink) {
            return send_request_raw("session/resume", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/list` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Listing filters / pagination.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_list(const v1::SessionListParams &params, OutboundSink &&sink) {
            return send_request_raw("session/list", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/delete` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session to delete.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_delete(const v1::SessionDeleteParams &params, OutboundSink &&sink) {
            return send_request_raw("session/delete", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/close` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session to close.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_close(const v1::SessionCloseParams &params, OutboundSink &&sink) {
            return send_request_raw("session/close", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/set_mode` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session and mode id.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_set_mode(const v1::SessionSetModeParams &params, OutboundSink &&sink) {
            return send_request_raw("session/set_mode", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/set_config_option` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session, option id and value.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t
        send_session_set_config_option(const v1::SessionSetConfigOptionParams &params, OutboundSink &&sink) {
            return send_request_raw("session/set_config_option", params, std::forward<OutboundSink>(sink));
        }

        // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
        /// Sends an ACP `session/set_model` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session and model id.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        /// @note UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema.
        template<typename OutboundSink>
        std::int64_t send_session_set_model(const v1::SessionSetModelParams &params, OutboundSink &&sink) {
            return send_request_raw("session/set_model", params, std::forward<OutboundSink>(sink));
        }

        /// Sends an ACP `session/prompt` request without registering a callback.
        /// The id is allocated from an internal counter; the agent's reply is delivered to
        /// `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session id and prompt content blocks.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_session_prompt(const v1::SessionPromptParams &params, OutboundSink &&sink) {
            return send_request_raw("session/prompt", params, std::forward<OutboundSink>(sink));
        }

        // Custom `_`-prefixed extension request
        /// Sends a custom extension request without registering a callback.
        /// The reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
        /// @param params Raw JSON parameters.
        /// @param sink   Outbound byte sink.
        /// @return The JSON-RPC request id used.
        template<typename OutboundSink>
        std::int64_t send_ext_method(std::string_view method, const RawJson &params, OutboundSink &&sink) {
            return send_request_raw(method, params, std::forward<OutboundSink>(sink));
        }

        // --- Typed Callback Outbound Requests (automatic decoding & ID correlation) ---
        // `initialize` responses are also recorded for agent_capabilities() / agent_info() / auth_methods()
        /// Sends an ACP `initialize` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::InitializeResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Protocol version, client capabilities and client info.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        /// @note On success the result is also recorded for agent_capabilities(), agent_info() and auth_methods() before @p cb runs.
        template<typename OutboundSink, typename Callback>
        auto send_initialize(const v1::InitializeParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::InitializeResult>(
                "initialize", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb),
                [this](const v1::InitializeResult &res) {
                    agent_capabilities_ = res.agentCapabilities;
                    agent_info_ = res.agentInfo;
                    auth_methods_ = res.authMethods;
                });
        }

        /// Sends an ACP `authenticate` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::AuthenticateResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Authentication method to use.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_authenticate(const v1::AuthenticateParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::AuthenticateResult>(
                "authenticate", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `logout` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::LogoutResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Logout parameters.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_logout(const v1::LogoutParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::LogoutResult>(
                "logout", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/new` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionNewResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Working directory and MCP servers for the new session.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_new(const v1::SessionNewParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionNewResult>(
                "session/new", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/load` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionLoadResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session to load.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_load(const v1::SessionLoadParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionLoadResult>(
                "session/load", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/resume` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionResumeResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session to resume.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_resume(const v1::SessionResumeParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionResumeResult>(
                "session/resume", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/list` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionListResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Listing filters / pagination.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_list(const v1::SessionListParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionListResult>(
                "session/list", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/delete` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionDeleteResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session to delete.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_delete(const v1::SessionDeleteParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionDeleteResult>(
                "session/delete", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/close` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionCloseResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session to close.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_close(const v1::SessionCloseParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionCloseResult>(
                "session/close", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/set_mode` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetModeResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session and mode id.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_set_mode(const v1::SessionSetModeParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionSetModeResult>(
                "session/set_mode", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/set_config_option` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetConfigOptionResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session, option id and value.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_set_config_option(const v1::SessionSetConfigOptionParams &params,
                                            OutboundSink &&sink,
                                            Callback &&cb) {
            return send_request_typed<v1::SessionSetConfigOptionResult>(
                "session/set_config_option", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
        /// Sends an ACP `session/set_model` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionSetModelResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session and model id.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        /// @note UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema.
        template<typename OutboundSink, typename Callback>
        auto send_session_set_model(const v1::SessionSetModelParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionSetModelResult>(
                "session/set_model", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends an ACP `session/prompt` request and invokes @p cb with the typed reply.
        /// The id is allocated from an internal counter and @p cb is registered as a pending request until a response
        /// with that id is fed in (or cancel_all_pending() is called).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<v1::SessionPromptResult, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param params Session id and prompt content blocks.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_session_prompt(const v1::SessionPromptParams &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<v1::SessionPromptResult>(
                "session/prompt", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
        }

        /// Sends a custom extension request and invokes @p cb with the raw JSON reply.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked once as `cb(expected<RawJson, RpcError>)` with the decoded result or the error reply. Or buraq::use_future.
        /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
        /// @param params Raw JSON parameters.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The JSON-RPC request id used, or a `std::future` of the outcome when @p cb is buraq::use_future.
        template<typename OutboundSink, typename Callback>
        auto send_ext_method(std::string_view method, const RawJson &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<RawJson>(method, params, std::forward<OutboundSink>(sink),
                                               std::forward<Callback>(cb));
        }

        // --- Outbound Notifications ---
        /// Sends a `session/cancel` notification to stop the ongoing prompt turn of a session.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Session whose prompt turn should be cancelled.
        /// @param sink   Outbound byte sink.
        template<typename OutboundSink>
        void send_session_cancel(const v1::SessionCancelParams &params, OutboundSink &&sink) {
            emit_bytes(Codec::encode_notification("session/cancel", params), std::forward<OutboundSink>(sink));
        }

        // Ask the agent to abandon an in-flight request (`$/cancel_request`). The pending callback stays
        // registered: the agent still answers, typically with -32800 (Request cancelled).
        /// Sends a `$/cancel_request` notification for an in-flight request.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param params Id of the request to cancel.
        /// @param sink   Outbound byte sink.
        template<typename OutboundSink>
        void send_cancel_request(const v1::CancelRequestParams &params, OutboundSink &&sink) {
            emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
        }

        /// Sends a custom extension notification to the agent.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
        /// @param params Raw JSON parameters.
        /// @param sink   Outbound byte sink.
        template<typename OutboundSink>
        void send_ext_notification(std::string_view method, const RawJson &params, OutboundSink &&sink) {
            emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
        }

        // --- Negotiated Agent Capabilities (populated by the typed send_initialize overload) ---
        /// Returns the agent capabilities negotiated by `initialize`.
        /// @return The `agentCapabilities` of a successful typed send_initialize() reply, or `std::nullopt` before one.
        [[nodiscard]] const std::optional<v1::AgentCapabilities> &agent_capabilities() const noexcept {
            return agent_capabilities_;
        }

        /// Returns the agent implementation info from `initialize`.
        /// @return The `agentInfo` of a successful typed send_initialize() reply, or `std::nullopt` if absent or before one.
        [[nodiscard]] const std::optional<v1::ImplementationInfo> &agent_info() const noexcept {
            return agent_info_;
        }

        /// Returns the authentication methods advertised by the agent in `initialize`.
        /// @return The `authMethods` of a successful typed send_initialize() reply; empty before one.
        [[nodiscard]] const std::vector<v1::AuthMethod> &auth_methods() const noexcept {
            return auth_methods_;
        }

        /// Returns the number of typed requests still awaiting a response.
        /// @return Size of the pending-request table.
        [[nodiscard]] std::size_t pending_count() const {
            return pending_.size();
        }

        // Fail and clear all in-flight requests (useful on transport disconnect)
        /// Fails and clears every in-flight typed request.
        /// Each pending callback is invoked with an error envelope carrying @p reason. Useful on transport disconnect.
        /// @param reason Error delivered to every pending callback; defaults to `-32800` (Request cancelled).
        void cancel_all_pending(const RpcError &reason = RpcError::request_cancelled()) {
            pending_.cancel_all(reason);
        }

        /// Sends any request (e.g. an unstable method this SDK does not model yet) and decodes the reply as @p Result.
        /// @tparam Result Result type to decode.
        /// @param method JSON-RPC method name.
        /// @param params Parameters (any type the codec can encode).
        /// @param sink   Outbound byte sink.
        /// @param cb     Callback `cb(expected<Result, RpcError>)` or buraq::use_future.
        /// @return The request id (callback) or a `std::future` of the outcome (buraq::use_future).
        template<typename Result, typename Params, typename OutboundSink, typename Callback>
        auto send_request(std::string_view method, const Params &params, OutboundSink &&sink, Callback &&cb) {
            return send_request_typed<Result>(method, params, std::forward<OutboundSink>(sink),
                                              std::forward<Callback>(cb));
        }

        // --- Inbound Frame Entry Point ---
        /// Processes one inbound JSON-RPC frame from the agent.
        /// Notifications go to dispatch_notification(), agent requests to dispatch_agent_request() (replies are
        /// written to @p sink) and responses to dispatch_response().
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param frame Bytes of a single JSON-RPC message (one NDJSON line, without the newline).
        /// @param sink  Outbound byte sink used to answer agent requests.
        /// @return Empty on success; an RpcError if the envelope cannot be parsed or classified (`-32600`), or if a
        ///         recognised notification fails to decode.
        template<typename OutboundSink>
        expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink &&sink) {
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
        /// Agent capabilities from the last successful typed `initialize`.
        std::optional<v1::AgentCapabilities> agent_capabilities_{};
        /// Agent implementation info from the last successful typed `initialize`.
        std::optional<v1::ImplementationInfo> agent_info_{};
        /// Authentication methods from the last successful typed `initialize`.
        std::vector<v1::AuthMethod> auth_methods_{};

        /// Writes an already-encoded JSON-RPC message to the outbound sink as raw bytes.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param wire Serialized JSON-RPC message.
        /// @param sink Outbound byte sink.
        template<typename OutboundSink>
        static void emit_bytes(std::string_view wire, OutboundSink &&sink) {
            sink(std::as_bytes(std::span{wire.data(), wire.size()}));
        }

        /// Allocates a request id and emits a request without registering a callback.
        /// @tparam Params Parameter type to encode.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param method JSON-RPC method name.
        /// @param params Request parameters.
        /// @param sink   Outbound byte sink.
        /// @return The allocated request id.
        template<typename Params, typename OutboundSink>
        std::int64_t send_request_raw(std::string_view method, const Params &params, OutboundSink &&sink) {
            const auto id = pending_.allocate();
            emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
            return id;
        }

        /// Allocates a request id, registers a decoding continuation and emits the request.
        /// The continuation forwards an error reply as `unexpected`, otherwise decodes `result` as @p Result.
        /// @tparam Result   Expected result type of the reply.
        /// @tparam Params   Parameter type to encode.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Callback Callable invoked as `cb(expected<Result, RpcError>)`.
        /// @param method JSON-RPC method name.
        /// @param params Request parameters.
        /// @param sink   Outbound byte sink.
        /// @param cb     Completion callback.
        /// @return The allocated request id.
        template<typename Result, typename Params, typename OutboundSink, typename Callback,
            typename OnValue = detail::ignore_value>
        auto send_request_typed(std::string_view method,
                                const Params &params,
                                OutboundSink &&sink,
                                Callback &&cb,
                                OnValue on_value = {}) {
            return detail::send_typed_request<Result, Codec>(
                pending_, std::forward<Callback>(cb), std::move(on_value),
                [&](const RequestId &id) { emit_bytes(Codec::encode_request(id, method, params), sink); });
        }

        /// Dispatches an agent notification to the matching `Derived` hook.
        /// Handles `session/update`, `elicitation/complete`, `$/cancel_request` and `_`-prefixed extensions;
        /// other notifications, or ones without a matching hook, are ignored.
        /// @param env Parsed notification envelope.
        /// @return Empty on success, or the decode error of a recognised notification.
        expected<void, RpcError> dispatch_notification(const RawEnvelope &env) {
            if (env.method == "session/update") {
                if constexpr (requires(Derived &d, const v1::SessionUpdateNotification &u)
                {
                    d.on_session_update(u);
                }) {
                    auto update = Codec::template decode<v1::SessionUpdateNotification>(env.params_json);
                    if (!update) {
                        // Forward-compat: newer agents may emit sessionUpdate kinds this SDK does not model yet
                        if constexpr (requires(Derived &d, const RawEnvelope &e, const RpcError &err)
                        {
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
                if constexpr (requires(Derived &d, const v1::CompleteElicitationParams &p)
                {
                    d.on_elicitation_complete(p);
                }) {
                    auto params = Codec::template decode<v1::CompleteElicitationParams>(env.params_json);
                    if (!params) return unexpected(params.error());
                    self().on_elicitation_complete(*params);
                }
                return {};
            }

            if (env.method == "$/cancel_request") {
                if constexpr (requires(Derived &d, const v1::CancelRequestParams &p) { d.on_cancel_request(p); }) {
                    auto params = Codec::template decode<v1::CancelRequestParams>(env.params_json);
                    if (!params) return unexpected(params.error());
                    self().on_cancel_request(*params);
                }
                return {};
            }

            if (!env.method.empty() && env.method.front() == '_') {
                if constexpr (requires(Derived &d, std::string_view m, const RawJson &p)
                {
                    d.on_ext_notification(m, p);
                }) {
                    self().on_ext_notification(env.method, RawJson{std::string(env.params_json)});
                }
            }
            return {};
        }

        /// Decodes, validates and answers a typed agent request.
        /// Decode failures are answered with the codec's error and `v1::validate` failures with `-32602`
        /// (Invalid params); otherwise the handler's `expected` is encoded as the result or error reply.
        /// @tparam Params  Parameter type to decode from `params`.
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @tparam Handler Callable `(Derived&, const Params&) -> expected<Result, RpcError>`.
        /// @param env     Parsed request envelope (must carry an id).
        /// @param sink    Outbound byte sink.
        /// @param handler Invokes the `Derived` hook.
        /// @return Always empty.
        template<typename Params, typename OutboundSink, typename Handler>
        expected<void, RpcError> handle_agent_request(const RawEnvelope &env, OutboundSink &sink, Handler &&handler) {
            auto params = Codec::template decode<Params>(env.params_json);
            if (!params) {
                emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
                return {};
            }
            if (auto reason = v1::validate(*params)) {
                emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
                return {};
            }

            auto res = detail::invoke_guarded([&] { return handler(self(), *params); });
            if (!res) {
                emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
            } else {
                emit_bytes(Codec::encode_result(*env.id, *res), sink);
            }
            return {};
        }

        /// Binds a Responder to request @p id, copying @p sink into it.
        template<typename Result, typename OutboundSink>
        static Responder<Result> make_responder(const RequestId &id, OutboundSink &sink) {
            static_assert(std::is_copy_constructible_v<std::remove_cvref_t<OutboundSink> >,
                          "Deferred (Responder) handlers keep a copy of the outbound sink: it must be copy-constructible "
                          "(wrap stateful sinks with std::ref or use a shared handle)");
            return Responder<Result>::template create<Codec>(id, OutboundFn(sink));
        }

        /// Decodes and validates an agent request, then hands a Responder to a deferred hook.
        /// Decode and validation failures are answered immediately; an exception escaping the hook fails the request
        /// with `-32603`.
        /// @tparam Params  Parameter type to decode from `params`.
        /// @tparam Result  Result type of the request.
        /// @tparam OutboundSink Copy-constructible sink type.
        /// @tparam Handler Callable `(Derived&, const Params&, Responder<Result>) -> void`.
        /// @param env     Parsed request envelope (must carry an id).
        /// @param sink    Outbound byte sink (copied into the Responder).
        /// @param handler Invokes the `Derived` hook.
        /// @return Always empty.
        template<typename Params, typename Result, typename OutboundSink, typename Handler>
        expected<void, RpcError> handle_agent_request_deferred(const RawEnvelope &env, OutboundSink &sink,
                                                               Handler &&handler) {
            auto params = Codec::template decode<Params>(env.params_json);
            if (!params) {
                emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
                return {};
            }
            if (auto reason = v1::validate(*params)) {
                emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
                return {};
            }
            detail::invoke_deferred(make_responder<Result>(*env.id, sink),
                                    [&](Responder<Result> &r) { handler(self(), *params, r); });
            return {};
        }

        /// Routes an agent request to the matching `Derived` hook by method name.
        /// Unimplemented methods are answered with `-32601` (Method not found).
        /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
        /// @param env  Parsed request envelope.
        /// @param sink Outbound byte sink.
        /// @return Always empty.
        template<typename OutboundSink>
        expected<void, RpcError> dispatch_agent_request(const RawEnvelope &env, OutboundSink &&sink) {
            // Routes an agent request to `Derived::HOOK`, preferring the deferred form `void HOOK(const PARAMS&, Responder<RESULT>)`
            // over the synchronous form `expected<RESULT, RpcError> HOOK(const PARAMS&)`.
#define BURAQ_CLIENT_ROUTE(NAME, HOOK, PARAMS, RESULT)                                                           \
    if (env.method == NAME) {                                                                                  \
        if constexpr (requires(Derived& d, const PARAMS& p, Responder<RESULT> r) {                             \
                          { d.HOOK(p, std::move(r)) } -> std::same_as<void>;                                    \
                      }) {                                                                                     \
            return handle_agent_request_deferred<PARAMS, RESULT>(                                              \
                env, sink, [](Derived& d, const PARAMS& p, Responder<RESULT> r) { d.HOOK(p, std::move(r)); }); \
        } else if constexpr (requires(Derived& d, const PARAMS& p) { d.HOOK(p); }) {                           \
            return handle_agent_request<PARAMS>(env, sink, [](Derived& d, const PARAMS& p) { return d.HOOK(p); }); \
        }                                                                                                      \
    }

            BURAQ_CLIENT_ROUTE("session/request_permission", on_request_permission, v1::RequestPermissionParams,
                               v1::RequestPermissionResult)
            BURAQ_CLIENT_ROUTE("fs/read_text_file", on_fs_read_text_file, v1::FsReadTextFileParams,
                               v1::FsReadTextFileResult)
            BURAQ_CLIENT_ROUTE("fs/write_text_file", on_fs_write_text_file, v1::FsWriteTextFileParams,
                               v1::FsWriteTextFileResult)
            BURAQ_CLIENT_ROUTE("terminal/create", on_terminal_create, v1::TerminalCreateParams,
                               v1::TerminalCreateResult)
            BURAQ_CLIENT_ROUTE("terminal/output", on_terminal_output, v1::TerminalOutputParams,
                               v1::TerminalOutputResult)
            BURAQ_CLIENT_ROUTE("terminal/wait_for_exit", on_terminal_wait_for_exit, v1::TerminalWaitForExitParams,
                               v1::TerminalWaitForExitResult)
            BURAQ_CLIENT_ROUTE("terminal/kill", on_terminal_kill, v1::TerminalKillParams, v1::TerminalKillResult)
            BURAQ_CLIENT_ROUTE("terminal/release", on_terminal_release, v1::TerminalReleaseParams,
                               v1::TerminalReleaseResult)
            BURAQ_CLIENT_ROUTE("elicitation/create", on_elicitation_create, v1::CreateElicitationParams,
                               v1::CreateElicitationResult)

#undef BURAQ_CLIENT_ROUTE

            if (!env.method.empty() && env.method.front() == '_') {
                if constexpr (requires(Derived &d, std::string_view m, const RawJson &p, Responder<RawJson> r)
                {
                    { d.on_ext_method(m, p, std::move(r)) } -> std::same_as<void>;
                }) {
                    std::string method{env.method};
                    RawJson params{std::string(env.params_json)};
                    detail::invoke_deferred(make_responder<RawJson>(*env.id, sink), [&](Responder<RawJson> &r) {
                        self().on_ext_method(method, params, r);
                    });
                    return {};
                } else if constexpr (requires(Derived &d, std::string_view m, const RawJson &p)
                {
                    { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError> >;
                }) {
                    auto res = detail::invoke_guarded(
                        [&] { return self().on_ext_method(env.method, RawJson{std::string(env.params_json)}); });
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

        /// Completes a pending typed request, or forwards the response to `on_raw_response`.
        /// Only integer ids are looked up in the pending table; the handler is removed before it is invoked.
        /// @param env Parsed response envelope.
        /// @return Always empty.
        expected<void, RpcError> dispatch_response(const RawEnvelope &env) {
            if (pending_.complete(env)) {
                return {};
            }

            if constexpr (requires(Derived &d, const RawEnvelope &r) { d.on_raw_response(r); }) {
                self().on_raw_response(env);
            }
            return {};
        }
    };
} // namespace buraq
