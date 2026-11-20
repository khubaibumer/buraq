#pragma once

#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <type_traits>
#include "buraq/core/async.hpp"
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

/// Root namespace of the buraq Agent Client Protocol (ACP) v1 SDK.
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
/// CRTP base class for implementing an ACP v1 agent over JSON-RPC 2.0.
///
/// `AgentBase` is sans-I/O: feed it one inbound frame at a time with feed() and it decodes the message,
/// validates the parameters, dispatches to the matching `Derived` handler and writes the encoded reply to the
/// supplied outbound sink. Every handler is optional and detected at compile time with `requires`
/// expressions; requests for methods `Derived` does not implement are answered with `-32601` (Method not found).
/// Handlers that return `expected<Result, RpcError>` have the value encoded as the JSON-RPC `result`, or the
/// error encoded as the JSON-RPC `error`.
///
/// Optional `Derived` hooks (Client -> Agent requests):
/// - `expected<v1::InitializeResult, RpcError> on_initialize(const v1::InitializeParams&)` -- `initialize`.
///   When present, every other request is rejected with `-32600` (Invalid Request) until it succeeds.
/// - `expected<v1::AuthenticateResult, RpcError> on_authenticate(const v1::AuthenticateParams&)` -- `authenticate`
/// - `expected<v1::LogoutResult, RpcError> on_logout(const v1::LogoutParams&)` -- `logout`
/// - `expected<v1::SessionNewResult, RpcError> on_session_new(const v1::SessionNewParams&)` -- `session/new`
/// - `expected<v1::SessionLoadResult, RpcError> on_session_load(const v1::SessionLoadParams&)` -- `session/load`
/// - `expected<v1::SessionResumeResult, RpcError> on_session_resume(const v1::SessionResumeParams&)` -- `session/resume`
/// - `expected<v1::SessionListResult, RpcError> on_session_list(const v1::SessionListParams&)` -- `session/list`
/// - `expected<v1::SessionDeleteResult, RpcError> on_session_delete(const v1::SessionDeleteParams&)` -- `session/delete`
/// - `expected<v1::SessionCloseResult, RpcError> on_session_close(const v1::SessionCloseParams&)` -- `session/close`
/// - `expected<v1::SessionSetModeResult, RpcError> on_session_set_mode(const v1::SessionSetModeParams&)` -- `session/set_mode`
/// - `expected<v1::SessionSetConfigOptionResult, RpcError> on_session_set_config_option(const v1::SessionSetConfigOptionParams&)`
///   -- `session/set_config_option`
/// - `expected<v1::SessionSetModelResult, RpcError> on_session_set_model(const v1::SessionSetModelParams&)`
///   -- `session/set_model` (unstable in ACP)
/// - `generator<v1::SessionUpdatePayload> on_session_prompt_stream(const v1::SessionPromptParams&)` -- `session/prompt`
///   (only when `BURAQ_HAS_STD_GENERATOR`); each yielded payload is sent as a `session/update` notification and the
///   request completes with `stopReason = "end_turn"`. Takes precedence over `on_session_prompt`.
/// - `expected<v1::SessionPromptResult, RpcError> on_session_prompt(const v1::SessionPromptParams&, Sink&)` or
///   `on_session_prompt(const v1::SessionPromptParams&)` -- `session/prompt`; the two-argument form receives the
///   outbound sink so it can stream updates with emit_session_update() and delegate requests to the client.
/// - `expected<RawJson, RpcError> on_ext_method(std::string_view method, const RawJson& params)` -- any `_`-prefixed request
///
/// Optional `Derived` hooks (Client -> Agent notifications and responses):
/// - `void on_session_cancel(const v1::SessionCancelParams&)` -- `session/cancel`
/// - `void on_cancel_request(const v1::CancelRequestParams&)` -- `$/cancel_request`
/// - `void on_ext_notification(std::string_view method, const RawJson& params)` -- any `_`-prefixed notification
/// - `void on_raw_response(const RawEnvelope&)` -- responses to agent-initiated requests sent with the raw
///   (untyped) helpers; typed requests complete their callback / future instead.
///
/// Deferred replies: every request hook may instead take a `Responder<Result>` by value and return `void`, e.g.
/// `void on_session_prompt(const v1::SessionPromptParams&, Responder<v1::SessionPromptResult>)`. The reply is then
/// sent whenever the responder is completed (from any thread); see Responder. An exception escaping any hook is
/// answered with `-32603` instead of propagating out of feed().
///
/// Agent -> Client requests (request_permission(), read_text_file(), write_text_file(), create_terminal(), ...,
/// send_request()) take a completion token: a callback `cb(expected<Result, RpcError>)` or buraq::use_future.
/// buraq::runtime::Connection / buraq::runtime::serve_stdio() run the I/O on their own threads, so hooks may
/// simply block on `use_future` replies.
///
/// @code
/// class EchoAgent : public buraq::AgentBase<EchoAgent, buraq::GlazeCodec> {
/// public:
///     buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
///     on_initialize(const buraq::v1::InitializeParams&) {
///         return buraq::v1::InitializeResult{
///             .agentInfo = buraq::v1::ImplementationInfo{"buraq-echo", "0.1.0"}};
///     }
///
///     buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
///     on_session_new(const buraq::v1::SessionNewParams&) {
///         return buraq::v1::SessionNewResult{.sessionId = "sess_1"};
///     }
///
///     template <typename Sink>
///     buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
///     on_session_prompt(const buraq::v1::SessionPromptParams& params, Sink& sink) {
///         emit_session_update(buraq::v1::SessionUpdateNotification{
///             .sessionId = params.sessionId,
///             .update = buraq::v1::AgentMessageChunk{
///                 .content = buraq::v1::TextContent{.text = "Echo from buraq!"}}}, sink);
///         return buraq::v1::SessionPromptResult{.stopReason = "end_turn"};
///     }
/// };
///
/// int main() {
///     EchoAgent agent;
///     return buraq::runtime::serve_stdio(agent); // or the single-threaded transport::run_stdio_agent(agent)
/// }
/// @endcode
///
/// @tparam Derived The concrete agent type (CRTP); must publicly derive from `AgentBase<Derived, Codec>`.
/// @tparam Codec   JSON codec satisfying the JsonCodec concept (e.g. `GlazeCodec`, `NlohmannCodec`).
/// @note Not internally synchronized: call feed() and the outbound helpers from one thread at a time.
/// @see ClientBase
template <typename Derived, JsonCodec Codec>
class AgentBase {
public:
    /// JSON codec this agent encodes and decodes with.
    using codec_type = Codec;

protected:
    /// Returns `*this` downcast to the derived agent type.
    /// @return Mutable reference to the `Derived` object.
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    /// Returns `*this` downcast to the derived agent type (const overload).
    /// @return Const reference to the `Derived` object.
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    /// Writes an already-encoded JSON-RPC message to the outbound sink as raw bytes.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param wire Serialized JSON-RPC message.
    /// @param sink Outbound byte sink.
    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

public:
    /// Processes one inbound JSON-RPC frame from the client.
    /// Parses the envelope and dispatches it: responses go to `on_raw_response`, requests and notifications to the
    /// matching `Derived` hook. Replies (results and protocol errors such as `-32601`, `-32602` or `-32600` before
    /// `initialize`) are written to @p sink rather than returned.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param frame Bytes of a single JSON-RPC message (one NDJSON line, without the newline).
    /// @param sink  Outbound byte sink that receives any reply.
    /// @return Empty on success; an RpcError if the frame could not be parsed as a JSON-RPC envelope, or if a
    ///         known request method arrived without an `id`. No reply is emitted in those cases.
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) {
            return unexpected(env.error());
        }
        return dispatch_message(*env, std::forward<OutboundSink>(sink));
    }

    // True once `initialize` has completed successfully on this connection
    /// Reports whether `initialize` has completed successfully on this connection.
    /// @return `true` once `on_initialize` returned a value; always `false` if `Derived` has no `on_initialize`.
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    // --- Negotiated Client Capabilities (populated by `initialize`) ---
    /// Returns the client capabilities negotiated by `initialize`.
    /// @return The `clientCapabilities` sent by the client, or `std::nullopt` before a successful `initialize`.
    [[nodiscard]] const std::optional<v1::ClientCapabilities>& client_capabilities() const noexcept {
        return client_capabilities_;
    }

    /// Returns the client implementation info sent with `initialize`.
    /// @return The `clientInfo` sent by the client, or `std::nullopt` if absent or before a successful `initialize`.
    [[nodiscard]] const std::optional<v1::ImplementationInfo>& client_info() const noexcept {
        return client_info_;
    }

    /// Reports whether the client advertised `fs.readTextFile`.
    /// @return `true` if request_fs_read() (`fs/read_text_file`) may be used.
    [[nodiscard]] bool client_can_read_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->readTextFile;
    }

    /// Reports whether the client advertised `fs.writeTextFile`.
    /// @return `true` if request_fs_write() (`fs/write_text_file`) may be used.
    [[nodiscard]] bool client_can_write_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->writeTextFile;
    }

    /// Reports whether the client advertised the `terminal` capability.
    /// @return `true` if the `terminal/*` requests (request_terminal_create(), ...) may be used.
    [[nodiscard]] bool client_supports_terminal() const noexcept {
        return client_capabilities_ && client_capabilities_->terminal;
    }

    /// Reports whether the client advertised form-based elicitation (`elicitation.form`).
    /// @return `true` if form-mode request_elicitation() may be used.
    [[nodiscard]] bool client_supports_elicitation_form() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->form;
    }

    /// Reports whether the client advertised URL-based elicitation (`elicitation.url`).
    /// @return `true` if URL-mode request_elicitation() may be used.
    [[nodiscard]] bool client_supports_elicitation_url() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->url;
    }

    /// Reports whether the client advertised terminal authentication (`auth.terminal`).
    /// @return `true` if the client can run terminal-based auth methods.
    [[nodiscard]] bool client_supports_terminal_auth() const noexcept {
        return client_capabilities_ && client_capabilities_->auth && client_capabilities_->auth->terminal;
    }

    /// Reports whether the client advertised boolean session config options (`session.configOptions.boolean`).
    /// @return `true` if boolean config options may be offered to the client.
    [[nodiscard]] bool client_supports_boolean_config_options() const noexcept {
        return client_capabilities_ && client_capabilities_->session &&
               client_capabilities_->session->configOptions && client_capabilities_->session->configOptions->boolean;
    }

    // --- Notifications (Agent -> Client) ---
    /// Sends a `session/update` notification to the client.
    /// Encodes and emits a JSON-RPC notification for ACP method `session/update`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param update Session id and update payload to stream.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void emit_session_update(const v1::SessionUpdateNotification& update, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/update", update), std::forward<OutboundSink>(sink));
    }

    /// Tells the client that an elicitation (identified by `elicitationId`) has completed.
    /// Encodes and emits a JSON-RPC notification for ACP method `elicitation/complete`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Identifies the completed elicitation.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void notify_elicitation_complete(const v1::CompleteElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("elicitation/complete", params), std::forward<OutboundSink>(sink));
    }

    // Ask the client to abandon a request this agent sent earlier (`$/cancel_request`)
    /// Asks the client to abandon a request this agent sent earlier.
    /// Encodes and emits a JSON-RPC notification for ACP method `$/cancel_request`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param params Id of the agent-initiated request to cancel.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension notification
    /// Sends a custom extension notification to the client.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Delegated Client Requests (Agent -> Client); replies arrive via on_raw_response ---
    /// Asks the client for permission to run a tool call.
    /// Encodes and emits a JSON-RPC request for ACP method `session/request_permission`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, tool call and permission options.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void request_permission(const RequestId& id, const v1::RequestPermissionParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "session/request_permission", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to read a text file.
    /// Encodes and emits a JSON-RPC request for ACP method `fs/read_text_file`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, path and optional line range.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_can_read_text_file().
    template <typename OutboundSink>
    void request_fs_read(const RequestId& id, const v1::FsReadTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/read_text_file", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to write a text file.
    /// Encodes and emits a JSON-RPC request for ACP method `fs/write_text_file`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, path and content.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_can_write_text_file().
    template <typename OutboundSink>
    void request_fs_write(const RequestId& id, const v1::FsWriteTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/write_text_file", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to start a command in a new terminal.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/create`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session, command, arguments and environment.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_create(const RequestId& id, const v1::TerminalCreateParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/create", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client for a terminal's current output.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/output`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_output(const RequestId& id, const v1::TerminalOutputParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/output", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to wait for a terminal's command to exit.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/wait_for_exit`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_wait_for_exit(const RequestId& id,
                                        const v1::TerminalWaitForExitParams& params,
                                        OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/wait_for_exit", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to kill a terminal's command.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/kill`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_kill(const RequestId& id, const v1::TerminalKillParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/kill", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to release a terminal and its resources.
    /// Encodes and emits a JSON-RPC request for ACP method `terminal/release`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Session and terminal id.
    /// @param sink   Outbound byte sink.
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink>
    void request_terminal_release(const RequestId& id, const v1::TerminalReleaseParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/release", params), std::forward<OutboundSink>(sink));
    }

    /// Asks the client to collect structured input from the user.
    /// Encodes and emits a JSON-RPC request for ACP method `elicitation/create`. The base does not track the id: the
    /// client's reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present, otherwise dropped.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller; used to correlate the reply in `on_raw_response`.
    /// @param params Elicitation mode, message and schema/URL.
    /// @param sink   Outbound byte sink.
    /// @note Check client_supports_elicitation_form() / client_supports_elicitation_url() first.
    template <typename OutboundSink>
    void request_elicitation(const RequestId& id, const v1::CreateElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "elicitation/create", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    /// Sends a custom extension request to the client.
    /// The reply is delivered to `Derived::on_raw_response(const RawEnvelope&)` if present.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param id     Request id chosen by the caller.
    /// @param method Extension method name; ACP reserves `_`-prefixed names for extensions.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    template <typename OutboundSink>
    void request_ext_method(const RequestId& id, std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, method, params), std::forward<OutboundSink>(sink));
    }

    // --- Typed Delegated Client Requests (automatic ids, decoded replies) ---
    // Pass a callback `cb(expected<Result, RpcError>)` (returns the request id) or buraq::use_future
    // (returns std::future<expected<Result, RpcError>>; block on it only from a thread that is not feeding frames).

    /// Asks the client for permission to run a tool call and decodes the reply.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message.
    /// @tparam Token Callback `cb(expected<v1::RequestPermissionResult, RpcError>)` or buraq::use_future.
    /// @param params Session, tool call and permission options.
    /// @param sink   Outbound byte sink.
    /// @param token  Completion callback or buraq::use_future.
    /// @return The request id (callback) or a `std::future` of the outcome (buraq::use_future).
    template <typename OutboundSink, typename Token>
    auto request_permission(const v1::RequestPermissionParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::RequestPermissionResult>("session/request_permission", params, sink,
                                                       std::forward<Token>(token));
    }

    /// Asks the client to read a text file and decodes the reply (see request_permission(params, sink, token)).
    /// @note Only valid if client_can_read_text_file().
    template <typename OutboundSink, typename Token>
    auto request_fs_read(const v1::FsReadTextFileParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::FsReadTextFileResult>("fs/read_text_file", params, sink, std::forward<Token>(token));
    }

    /// Asks the client to write a text file and decodes the reply (see request_permission(params, sink, token)).
    /// @note Only valid if client_can_write_text_file().
    template <typename OutboundSink, typename Token>
    auto request_fs_write(const v1::FsWriteTextFileParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::FsWriteTextFileResult>("fs/write_text_file", params, sink, std::forward<Token>(token));
    }

    /// Asks the client to start a command in a new terminal (see request_permission(params, sink, token)).
    /// @note Only valid if client_supports_terminal().
    template <typename OutboundSink, typename Token>
    auto request_terminal_create(const v1::TerminalCreateParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::TerminalCreateResult>("terminal/create", params, sink, std::forward<Token>(token));
    }

    /// Asks the client for a terminal's current output (see request_permission(params, sink, token)).
    template <typename OutboundSink, typename Token>
    auto request_terminal_output(const v1::TerminalOutputParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::TerminalOutputResult>("terminal/output", params, sink, std::forward<Token>(token));
    }

    /// Asks the client to wait for a terminal's command to exit (see request_permission(params, sink, token)).
    template <typename OutboundSink, typename Token>
    auto request_terminal_wait_for_exit(const v1::TerminalWaitForExitParams& params, OutboundSink&& sink,
                                        Token&& token) {
        return send_typed<v1::TerminalWaitForExitResult>("terminal/wait_for_exit", params, sink,
                                                         std::forward<Token>(token));
    }

    /// Asks the client to kill a terminal's command (see request_permission(params, sink, token)).
    template <typename OutboundSink, typename Token>
    auto request_terminal_kill(const v1::TerminalKillParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::TerminalKillResult>("terminal/kill", params, sink, std::forward<Token>(token));
    }

    /// Asks the client to release a terminal (see request_permission(params, sink, token)).
    template <typename OutboundSink, typename Token>
    auto request_terminal_release(const v1::TerminalReleaseParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::TerminalReleaseResult>("terminal/release", params, sink, std::forward<Token>(token));
    }

    /// Asks the client to collect structured input from the user (see request_permission(params, sink, token)).
    template <typename OutboundSink, typename Token>
    auto request_elicitation(const v1::CreateElicitationParams& params, OutboundSink&& sink, Token&& token) {
        return send_typed<v1::CreateElicitationResult>("elicitation/create", params, sink,
                                                       std::forward<Token>(token));
    }

    /// Sends a custom `_`-prefixed extension request and delivers the raw JSON result.
    /// @param method Extension method name.
    /// @param params Raw JSON parameters.
    /// @param sink   Outbound byte sink.
    /// @param token  Callback `cb(expected<RawJson, RpcError>)` or buraq::use_future.
    template <typename OutboundSink, typename Token>
    auto request_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink, Token&& token) {
        return send_typed<RawJson>(method, params, sink, std::forward<Token>(token));
    }

    /// Sends any request (e.g. an unstable method this SDK does not model yet) and decodes the reply as @p Result.
    /// @tparam Result Result type to decode.
    /// @param method JSON-RPC method name.
    /// @param params Parameters (any type the codec can encode).
    /// @param sink   Outbound byte sink.
    /// @param token  Callback `cb(expected<Result, RpcError>)` or buraq::use_future.
    template <typename Result, typename Params, typename OutboundSink, typename Token>
    auto send_request(std::string_view method, const Params& params, OutboundSink&& sink, Token&& token) {
        return send_typed<Result>(method, params, sink, std::forward<Token>(token));
    }

    /// Returns the number of typed agent -> client requests still awaiting a reply.
    [[nodiscard]] std::size_t pending_count() const { return pending_.size(); }

    /// Fails and clears every in-flight typed request (useful on transport disconnect).
    /// @param reason Error delivered to every pending continuation; defaults to `-32800` (Request cancelled).
    void cancel_all_pending(const RpcError& reason = RpcError::request_cancelled()) { pending_.cancel_all(reason); }

private:
    /// Set when `on_initialize` succeeds; gates all other requests when `Derived` implements `on_initialize`.
    detail::AtomicFlag initialized_{};
    /// Typed agent -> client requests awaiting a reply.
    detail::PendingRequests pending_{};
    /// Client capabilities from the last successful `initialize`.
    std::optional<v1::ClientCapabilities> client_capabilities_{};
    /// Client implementation info from the last successful `initialize`.
    std::optional<v1::ImplementationInfo> client_info_{};

    /// Routes a parsed envelope to the matching `Derived` hook by method name.
    /// Handles the `initialize` gate, built-in ACP methods, `_`-prefixed extensions and the `-32601` fallback.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param env  Parsed inbound envelope.
    /// @param sink Outbound byte sink.
    /// @return Empty on success, or the error from handle_request().
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_message(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.is_response()) {
            if (pending_.complete(env)) {
                return {};
            }
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
                        auto result = detail::invoke_guarded([&] { return d.on_initialize(p); });
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

// Routes a request to `Derived::HOOK`, preferring the deferred form `void HOOK(const PARAMS&, Responder<RESULT>)`
// over the synchronous form `expected<RESULT, RpcError> HOOK(const PARAMS&)`.
#define BURAQ_AGENT_ROUTE(NAME, HOOK, PARAMS, RESULT)                                                            \
    if (env.method == NAME) {                                                                                  \
        if constexpr (requires(Derived& d, const PARAMS& p, Responder<RESULT> r) {                             \
                          { d.HOOK(p, std::move(r)) } -> std::same_as<void>;                                    \
                      }) {                                                                                     \
            return handle_deferred<PARAMS, RESULT>(                                                            \
                env, sink, [](Derived& d, const PARAMS& p, Responder<RESULT> r) { d.HOOK(p, std::move(r)); }); \
        } else if constexpr (requires(Derived& d, const PARAMS& p) { d.HOOK(p); }) {                           \
            return handle_request<PARAMS>(env, sink, [](Derived& d, const PARAMS& p, auto&) { return d.HOOK(p); }); \
        }                                                                                                      \
    }

        BURAQ_AGENT_ROUTE("authenticate", on_authenticate, v1::AuthenticateParams, v1::AuthenticateResult)
        BURAQ_AGENT_ROUTE("logout", on_logout, v1::LogoutParams, v1::LogoutResult)
        BURAQ_AGENT_ROUTE("session/new", on_session_new, v1::SessionNewParams, v1::SessionNewResult)
        BURAQ_AGENT_ROUTE("session/load", on_session_load, v1::SessionLoadParams, v1::SessionLoadResult)
        BURAQ_AGENT_ROUTE("session/resume", on_session_resume, v1::SessionResumeParams, v1::SessionResumeResult)
        BURAQ_AGENT_ROUTE("session/list", on_session_list, v1::SessionListParams, v1::SessionListResult)
        BURAQ_AGENT_ROUTE("session/delete", on_session_delete, v1::SessionDeleteParams, v1::SessionDeleteResult)
        BURAQ_AGENT_ROUTE("session/close", on_session_close, v1::SessionCloseParams, v1::SessionCloseResult)
        BURAQ_AGENT_ROUTE("session/set_mode", on_session_set_mode, v1::SessionSetModeParams, v1::SessionSetModeResult)
        BURAQ_AGENT_ROUTE("session/set_config_option", on_session_set_config_option, v1::SessionSetConfigOptionParams,
                          v1::SessionSetConfigOptionResult)
        BURAQ_AGENT_ROUTE("session/set_model", on_session_set_model, v1::SessionSetModelParams,
                          v1::SessionSetModelResult)

#undef BURAQ_AGENT_ROUTE

        if (env.method == "session/prompt") {
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p,
                                   Responder<v1::SessionPromptResult> r) {
                { d.on_session_prompt(p, std::move(r)) } -> std::same_as<void>;
            }) {
                return handle_deferred<v1::SessionPromptParams, v1::SessionPromptResult>(
                    env, sink, [](Derived& d, const v1::SessionPromptParams& p, Responder<v1::SessionPromptResult> r) {
                        d.on_session_prompt(p, std::move(r));
                    });
            }
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

    /// Dispatches a `_`-prefixed extension request or notification.
    /// Requests go to `on_ext_method` (or are answered with `-32601`); notifications go to `on_ext_notification`
    /// or are ignored.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @param env  Parsed inbound envelope whose method starts with `_`.
    /// @param sink Outbound byte sink.
    /// @return Always empty.
    template <typename OutboundSink>
    expected<void, RpcError> dispatch_extension(const RawEnvelope& env, OutboundSink& sink) {
        const RawJson params{std::string(env.params_json)};
        if (env.id.has_value()) {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p, Responder<RawJson> r) {
                { d.on_ext_method(m, p, std::move(r)) } -> std::same_as<void>;
            }) {
                std::string method{env.method};
                detail::invoke_deferred(make_responder<RawJson>(*env.id, sink), [&](Responder<RawJson>& r) {
                    self().on_ext_method(method, params, r);
                });
                return {};
            } else if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError>>;
            }) {
                auto result = detail::invoke_guarded([&] { return self().on_ext_method(env.method, params); });
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

    /// Decodes, validates and answers a typed request.
    /// Decode failures are answered with the codec's error and `v1::validate` failures with `-32602`
    /// (Invalid params); otherwise the handler's `expected` is encoded as the result or error reply.
    /// @tparam Params  Parameter type to decode from `params`.
    /// @tparam OutboundSink Callable invoked as `sink(std::span<const std::byte>)` with one serialized JSON-RPC message (NDJSON newline framing is left to the transport).
    /// @tparam Handler Callable `(Derived&, const Params&, OutboundSink&) -> expected<Result, RpcError>`.
    /// @param env     Parsed inbound request envelope.
    /// @param sink    Outbound byte sink.
    /// @param handler Invokes the `Derived` hook.
    /// @return Empty on success; `-32600` (Invalid Request) if the envelope has no `id`.
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

        auto result = detail::invoke_guarded([&] { return handler(self(), *params, sink); });
        if (!result) {
            emit_bytes(Codec::encode_error(*env.id, result.error()), sink);
        } else {
            emit_bytes(Codec::encode_result(*env.id, *result), sink);
        }
        return {};
    }

    /// Binds a Responder to request @p id, copying @p sink into it.
    /// @tparam Result Result type of the request.
    /// @tparam OutboundSink Copy-constructible sink type.
    template <typename Result, typename OutboundSink>
    static Responder<Result> make_responder(const RequestId& id, OutboundSink& sink) {
        static_assert(std::is_copy_constructible_v<std::remove_cvref_t<OutboundSink>>,
                      "Deferred (Responder) handlers keep a copy of the outbound sink: it must be copy-constructible "
                      "(wrap stateful sinks with std::ref or use a shared handle)");
        return Responder<Result>::template create<Codec>(id, OutboundFn(sink));
    }

    /// Decodes and validates a request, then hands a Responder to a deferred hook.
    /// Decode and validation failures are answered immediately; otherwise the reply is sent whenever the hook
    /// (or a copy of its Responder) completes it. An exception escaping the hook fails the request with `-32603`.
    /// @tparam Params  Parameter type to decode from `params`.
    /// @tparam Result  Result type of the request.
    /// @tparam OutboundSink Copy-constructible sink type.
    /// @tparam Handler Callable `(Derived&, const Params&, Responder<Result>) -> void`.
    /// @param env     Parsed inbound request envelope.
    /// @param sink    Outbound byte sink (copied into the Responder).
    /// @param handler Invokes the `Derived` hook.
    /// @return Empty on success; `-32600` (Invalid Request) if the envelope has no `id`.
    template <typename Params, typename Result, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_deferred(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
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

        detail::invoke_deferred(make_responder<Result>(*env.id, sink),
                                [&](Responder<Result>& r) { handler(self(), *params, r); });
        return {};
    }

    /// Allocates an id, registers a typed continuation and emits an agent -> client request.
    template <typename Result, typename Params, typename OutboundSink, typename Token>
    auto send_typed(std::string_view method, const Params& params, OutboundSink& sink, Token&& token) {
        return detail::send_typed_request<Result, Codec>(
            pending_, std::forward<Token>(token), detail::ignore_value{},
            [&](const RequestId& id) { emit_bytes(Codec::encode_request(id, method, params), sink); });
    }
};

} // namespace buraq
