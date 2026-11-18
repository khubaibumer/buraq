#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/codecs/nlohmann.hpp>

// Always-on check: CI builds Release (NDEBUG), which would compile plain CHECK() away.
#define CHECK(expr)                                                                         \
    do {                                                                                    \
        if (!(expr)) {                                                                      \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr << '\n';  \
            std::abort();                                                                   \
        }                                                                                   \
    } while (false)

namespace {

// Helper to convert a std::string to std::span<const std::byte>
std::span<const std::byte> as_bytes(std::string_view sv) {
    return std::as_bytes(std::span{sv.data(), sv.size()});
}

// Helper sink that captures outbound frames as std::string
struct CaptureSink {
    std::vector<std::string>& frames;

    void operator()(std::span<const std::byte> bytes) const {
        frames.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
};

// --- Full-Featured Test Agent (implements optional ACP v1 methods) ---
template <buraq::JsonCodec Codec>
class FullTestAgent : public buraq::AgentBase<FullTestAgent<Codec>, Codec> {
public:
    bool cancel_received{false};
    std::string last_mode{};
    std::string last_model{};
    std::vector<buraq::RawEnvelope> received_responses{};

    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams& params) {
        CHECK(params.protocolVersion == buraq::v1::kProtocolVersion);
        return buraq::v1::InitializeResult{
            .agentCapabilities = buraq::v1::AgentCapabilities{
                .loadSession = true,
                .promptCapabilities = buraq::v1::AgentCapabilities::PromptCapabilities{
                    .image = true,
                    .audio = true,
                    .embeddedContext = true,
                },
            },
            .agentInfo = buraq::v1::ImplementationInfo{"buraq-test-agent", "1.0.0"},
        };
    }

    buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
    on_session_new(const buraq::v1::SessionNewParams& params) {
        CHECK(params.cwd == "/workspace/buraq");
        return buraq::v1::SessionNewResult{
            .sessionId = "sess_42",
            .modes = buraq::v1::SessionModeState{
                .currentModeId = "code",
                .availableModes = {
                    {"code", "Code", "Full editing"},
                    {"ask", "Ask", "Read-only Q&A"},
                },
            },
            .models = buraq::v1::SessionModelState{
                .currentModelId = "claude-sonnet",
                .availableModels = {
                    {"claude-sonnet", "Claude Sonnet", std::nullopt},
                },
            },
        };
    }

    buraq::expected<buraq::v1::SessionLoadResult, buraq::RpcError>
    on_session_load(const buraq::v1::SessionLoadParams& params) {
        if (params.sessionId != "sess_42") {
            return buraq::unexpected(buraq::RpcError::invalid_params("Unknown session"));
        }
        return buraq::v1::SessionLoadResult{};
    }

    buraq::expected<buraq::v1::SessionSetModeResult, buraq::RpcError>
    on_session_set_mode(const buraq::v1::SessionSetModeParams& params) {
        last_mode = params.modeId;
        return buraq::v1::SessionSetModeResult{};
    }

    buraq::expected<buraq::v1::SessionSetModelResult, buraq::RpcError>
    on_session_set_model(const buraq::v1::SessionSetModelParams& params) {
        last_model = params.modelId;
        return buraq::v1::SessionSetModelResult{};
    }

    template <typename Sink>
    buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
    on_session_prompt(const buraq::v1::SessionPromptParams& params, Sink& sink) {
        // 1. Emit PlanUpdate
        this->emit_session_update(buraq::v1::SessionUpdateNotification{
            .sessionId = params.sessionId,
            .update = buraq::v1::PlanUpdate{
                .entries = {
                    {"Inspect schema_v1.hpp", "high", "completed"},
                    {"Run dispatch_test", "high", "in_progress"},
                },
            },
        }, sink);

        // 2. Emit AgentThoughtChunk
        this->emit_session_update(buraq::v1::SessionUpdateNotification{
            .sessionId = params.sessionId,
            .update = buraq::v1::AgentThoughtChunk{
                .content = buraq::v1::TextContent{.text = "Checking multimodal prompt blocks..."},
            },
        }, sink);

        // 3. Emit ToolCallUpdate with diff content
        this->emit_session_update(buraq::v1::SessionUpdateNotification{
            .sessionId = params.sessionId,
            .update = buraq::v1::ToolCallUpdate{
                .toolCallId = "tc_1",
                .title = "Edit main.cpp",
                .kind = "edit",
                .status = "in_progress",
                .content = std::vector<buraq::v1::ToolCallContent>{
                    buraq::v1::ToolCallDiffItem{
                        .path = "src/main.cpp",
                        .oldText = "int x = 0;",
                        .newText = "int x = 42;",
                    },
                },
                .locations = std::vector<buraq::v1::ToolCallLocation>{
                    {.path = "src/main.cpp", .line = 12},
                },
            },
        }, sink);

        // 4. Emit AgentMessageChunk
        this->emit_session_update(buraq::v1::SessionUpdateNotification{
            .sessionId = params.sessionId,
            .update = buraq::v1::AgentMessageChunk{
                .content = buraq::v1::TextContent{.text = "Done!"},
            },
        }, sink);

        return buraq::v1::SessionPromptResult{.stopReason = "end_turn"};
    }

    void on_session_cancel(const buraq::v1::SessionCancelParams&) {
        cancel_received = true;
    }

    void on_raw_response(const buraq::RawEnvelope& env) {
        received_responses.push_back(env);
    }
};

// --- Minimal Agent (verifies compile-time method_not_found fallback) ---
template <buraq::JsonCodec Codec>
class MinimalAgent : public buraq::AgentBase<MinimalAgent<Codec>, Codec> {
public:
    bool reject_initialize{false};

    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams& params) {
        if (reject_initialize) {
            return buraq::unexpected(buraq::RpcError::invalid_params("rejected"));
        }
        if (params.protocolVersion != buraq::v1::kProtocolVersion) {
            return buraq::unexpected(buraq::RpcError::invalid_params("bad version"));
        }
        return buraq::v1::InitializeResult{};
    }
    // Intentionally omits on_session_load, on_session_set_mode, etc.
};

// --- Full-Featured Test Client (handles updates, permissions, FS, and terminals) ---
template <buraq::JsonCodec Codec>
class FullTestClient : public buraq::ClientBase<FullTestClient<Codec>, Codec> {
public:
    std::vector<buraq::v1::SessionUpdateNotification> updates{};
    std::vector<std::int64_t> response_ids{};
    bool permission_requested{false};
    bool terminal_created{false};

    void on_session_update(const buraq::v1::SessionUpdateNotification& notif) {
        updates.push_back(notif);
    }

    buraq::expected<buraq::v1::RequestPermissionResult, buraq::RpcError>
    on_request_permission(const buraq::v1::RequestPermissionParams& params) {
        permission_requested = true;
        CHECK(!params.options.empty());
        return buraq::v1::RequestPermissionResult{
            .outcome = buraq::v1::PermissionSelectedOutcome{
                .optionId = params.options.front().optionId,
            },
        };
    }

    buraq::expected<buraq::v1::FsReadTextFileResult, buraq::RpcError>
    on_fs_read_text_file(const buraq::v1::FsReadTextFileParams& params) {
        return buraq::v1::FsReadTextFileResult{
            .content = "// contents of " + params.path,
        };
    }

    buraq::expected<buraq::v1::TerminalCreateResult, buraq::RpcError>
    on_terminal_create(const buraq::v1::TerminalCreateParams& params) {
        terminal_created = true;
        CHECK(params.command == "cmake");
        return buraq::v1::TerminalCreateResult{.terminalId = "term_99"};
    }

    void on_raw_response(const buraq::RawEnvelope& env) {
        CHECK(!env.error.has_value());
        if (env.id && std::holds_alternative<std::int64_t>(*env.id)) {
            response_ids.push_back(std::get<std::int64_t>(*env.id));
        }
    }
};

// --- Client opting into forward-compatible handling of unmodelled sessionUpdate kinds ---
template <buraq::JsonCodec Codec>
class LenientClient : public buraq::ClientBase<LenientClient<Codec>, Codec> {
public:
    std::vector<buraq::v1::SessionUpdateNotification> updates{};
    std::vector<std::string> unhandled{};

    void on_session_update(const buraq::v1::SessionUpdateNotification& notif) {
        updates.push_back(notif);
    }

    void on_unhandled_session_update(const buraq::RawEnvelope& env, const buraq::RpcError&) {
        unhandled.emplace_back(env.params_json);
    }
};

template <buraq::JsonCodec Codec>
void run_protocol_hardening_suite() {
    std::vector<std::string> out;
    CaptureSink sink{out};

    // 1. Unknown keys / `_meta` extensions at every nesting level are ignored on decode
    {
        MinimalAgent<Codec> agent;
        CHECK(!agent.client_capabilities().has_value());
        CHECK(!agent.client_can_read_text_file() && !agent.client_supports_terminal());

        const std::string init =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","_meta":{"trace":"abc"},"params":{)"
            R"("_meta":{"zed.dev/x":true},"protocolVersion":1,"futureField":[1,{"a":null}],)"
            R"("clientCapabilities":{"_meta":{"jetbrains":{}},"fs":{"readTextFile":true,"writeTextFile":false,"_meta":{}},)"
            R"("terminal":true,"auth":{"terminal":true}},"clientInfo":{"name":"zed","version":"0.200.0","title":"Zed","_meta":null}}})";
        CHECK(agent.feed(as_bytes(init), sink).has_value());
        CHECK(out.size() == 1);
        auto env = Codec::parse_envelope(as_bytes(out.back()));
        CHECK(env.has_value() && !env->error.has_value());

        // 2. Negotiated client capabilities are retained for fs/* and terminal/* gating
        CHECK(agent.client_capabilities().has_value());
        CHECK(agent.client_can_read_text_file());
        CHECK(!agent.client_can_write_text_file());
        CHECK(agent.client_supports_terminal());
        CHECK(agent.client_info().has_value() && agent.client_info()->name == "zed");
    }

    // 3. A rejected initialize does not leave stale capabilities behind
    {
        MinimalAgent<Codec> agent;
        agent.reject_initialize = true;
        out.clear();
        const std::string init =
            R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":1,)"
            R"("clientCapabilities":{"fs":{"readTextFile":true,"writeTextFile":true},"terminal":true}}})";
        CHECK(agent.feed(as_bytes(init), sink).has_value());
        auto env = Codec::parse_envelope(as_bytes(out.back()));
        CHECK(env.has_value() && env->error.has_value());
        CHECK(!agent.client_capabilities().has_value());
        CHECK(!agent.client_can_read_text_file() && !agent.client_supports_terminal());
    }

    // 4. `_meta` and `annotations` inside tagged-union content blocks, tag not first
    {
        auto prompt = Codec::template decode<buraq::v1::SessionPromptParams>(
            R"({"_meta":{},"sessionId":"s","prompt":[)"
            R"({"text":"hi","type":"text","annotations":{"audience":["user"]},"_meta":{"k":[1,2]}},)"
            R"({"type":"resource_link","uri":"file:///a","name":"a","_meta":{}}]})");
        CHECK(prompt.has_value() && prompt->prompt.size() == 2);
        CHECK(std::holds_alternative<buraq::v1::TextContent>(prompt->prompt[0]));
        CHECK(std::get<buraq::v1::TextContent>(prompt->prompt[0]).text == "hi");

        auto perm = Codec::template decode<buraq::v1::RequestPermissionResult>(
            R"({"_meta":{},"outcome":{"outcome":"selected","optionId":"allow","_meta":{}}})");
        CHECK(perm.has_value());
        CHECK(std::holds_alternative<buraq::v1::PermissionSelectedOutcome>(perm->outcome));
    }

    // 5. current_mode_update round-trips; unmodelled sessionUpdate kinds reach the opt-in hook
    {
        LenientClient<Codec> client;
        const std::string mode_update =
            R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s","update":)"
            R"({"sessionUpdate":"current_mode_update","currentModeId":"ask","_meta":{}}}})";
        CHECK(client.feed(as_bytes(mode_update), sink).has_value());
        CHECK(client.updates.size() == 1);
        const auto* mode = std::get_if<buraq::v1::CurrentModeUpdate>(&client.updates[0].update);
        CHECK(mode != nullptr && mode->currentModeId == "ask");

        const auto encoded = Codec::encode_notification("session/update", client.updates[0]);
        auto reparsed_env = Codec::parse_envelope(as_bytes(encoded));
        CHECK(reparsed_env.has_value());
        auto reparsed = Codec::template decode<buraq::v1::SessionUpdateNotification>(reparsed_env->params_json);
        CHECK(reparsed.has_value() && *reparsed == client.updates[0]);

        const std::string future_update =
            R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s","update":)"
            R"({"sessionUpdate":"some_future_update","payload":{"x":1}}}})";
        CHECK(client.feed(as_bytes(future_update), sink).has_value());
        CHECK(client.unhandled.size() == 1 && client.updates.size() == 1);

        // Without the hook, the decode failure is surfaced to the transport loop
        FullTestClient<Codec> strict;
        CHECK(!strict.feed(as_bytes(future_update), sink).has_value());
    }
}

// --- Stable ACP v1 surface: auth, session lifecycle, config options, elicitation, cancellation, extensions ---
template <buraq::JsonCodec Codec>
class StableAgent : public buraq::AgentBase<StableAgent<Codec>, Codec> {
public:
    std::vector<std::string> sessions{};
    std::string authenticated_with{};
    bool logged_out{false};
    std::vector<buraq::RequestId> cancelled_requests{};
    std::vector<std::string> ext_notifications{};
    std::vector<buraq::RawEnvelope> responses{};
    std::vector<std::string> response_frames{};

    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams&) {
        return buraq::v1::InitializeResult{
            .agentCapabilities = buraq::v1::AgentCapabilities{
                .loadSession = false,
                .mcpCapabilities = buraq::v1::McpCapabilities{.http = true, .sse = false},
                .sessionCapabilities = buraq::v1::SessionCapabilities{
                    .list = buraq::v1::CapabilityMarker{},
                    .delete_ = buraq::v1::CapabilityMarker{},
                    .resume = buraq::v1::CapabilityMarker{},
                    .close = buraq::v1::CapabilityMarker{},
                },
                .auth = buraq::v1::AgentAuthCapabilities{.logout = buraq::v1::CapabilityMarker{}},
            },
            .authMethods = {
                buraq::v1::AuthMethodAgent{.id = "oauth", .name = "Sign in"},
                buraq::v1::AuthMethodTerminal{
                    .id = "cli-login", .name = "CLI login",
                    .args = std::vector<std::string>{"--login"},
                    .env = std::map<std::string, std::string>{{"MODE", "tty"}},
                },
            },
            .agentInfo = buraq::v1::ImplementationInfo{"stable-agent", "0.1.0"},
        };
    }

    buraq::expected<buraq::v1::AuthenticateResult, buraq::RpcError>
    on_authenticate(const buraq::v1::AuthenticateParams& p) {
        if (p.methodId != "oauth" && p.methodId != "cli-login") {
            return buraq::unexpected(buraq::RpcError::invalid_params("unknown auth method"));
        }
        authenticated_with = p.methodId;
        return buraq::v1::AuthenticateResult{};
    }

    buraq::expected<buraq::v1::LogoutResult, buraq::RpcError> on_logout(const buraq::v1::LogoutParams&) {
        logged_out = true;
        return buraq::v1::LogoutResult{};
    }

    buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
    on_session_new(const buraq::v1::SessionNewParams& p) {
        CHECK(p.mcpServers.size() == 3);
        CHECK(std::holds_alternative<buraq::v1::McpServerHttp>(p.mcpServers[0]));
        CHECK(std::get<buraq::v1::McpServerHttp>(p.mcpServers[0]).headers.at(0).name == "Authorization");
        CHECK(std::holds_alternative<buraq::v1::McpServerSse>(p.mcpServers[1]));
        CHECK(std::holds_alternative<buraq::v1::McpServerStdio>(p.mcpServers[2]));
        CHECK(std::get<buraq::v1::McpServerStdio>(p.mcpServers[2]).env.at(0).value == "1");
        CHECK(p.additionalDirectories && p.additionalDirectories->at(0) == "/srv/shared");
        CHECK(p.systemPrompt && p.systemPrompt->mode == "append" && p.systemPrompt->content.size() == 1);
        sessions.push_back("sess_" + std::to_string(sessions.size() + 1));
        return buraq::v1::SessionNewResult{
            .sessionId = sessions.back(),
            .configOptions = std::vector<buraq::v1::SessionConfigOption>{
                {
                    .id = "model", .name = "Model", .category = "model", .type = "select",
                    .currentValue = std::string("fast"),
                    .options = std::vector<buraq::v1::SessionConfigSelectItem>{
                        {.value = "fast", .name = "Fast"},
                        {.name = "Large", .group = "large",
                         .options = std::vector<buraq::v1::SessionConfigSelectOption>{{"opus", "Opus"}}},
                    },
                },
                {.id = "thinking", .name = "Thinking", .type = "boolean", .currentValue = false},
            },
        };
    }

    buraq::expected<buraq::v1::SessionResumeResult, buraq::RpcError>
    on_session_resume(const buraq::v1::SessionResumeParams& p) {
        if (std::find(sessions.begin(), sessions.end(), p.sessionId) == sessions.end()) {
            return buraq::unexpected(buraq::RpcError::session_not_found(p.sessionId));
        }
        return buraq::v1::SessionResumeResult{};
    }

    buraq::expected<buraq::v1::SessionListResult, buraq::RpcError>
    on_session_list(const buraq::v1::SessionListParams& p) {
        buraq::v1::SessionListResult res;
        for (const auto& id : sessions) {
            res.sessions.push_back({.sessionId = id, .cwd = p.cwd.value_or("/w"), .title = "T " + id});
        }
        res.nextCursor = "c2";
        return res;
    }

    buraq::expected<buraq::v1::SessionDeleteResult, buraq::RpcError>
    on_session_delete(const buraq::v1::SessionDeleteParams& p) {
        auto it = std::find(sessions.begin(), sessions.end(), p.sessionId);
        if (it == sessions.end()) {
            return buraq::unexpected(buraq::RpcError::session_not_found(p.sessionId));
        }
        sessions.erase(it);
        return buraq::v1::SessionDeleteResult{};
    }

    buraq::expected<buraq::v1::SessionCloseResult, buraq::RpcError>
    on_session_close(const buraq::v1::SessionCloseParams& p) {
        if (std::find(sessions.begin(), sessions.end(), p.sessionId) == sessions.end()) {
            return buraq::unexpected(buraq::RpcError::session_not_found(p.sessionId));
        }
        return buraq::v1::SessionCloseResult{};
    }

    buraq::expected<buraq::v1::SessionSetConfigOptionResult, buraq::RpcError>
    on_session_set_config_option(const buraq::v1::SessionSetConfigOptionParams& p) {
        CHECK(p.configId == "thinking");
        CHECK(std::holds_alternative<bool>(p.value) && std::get<bool>(p.value));
        return buraq::v1::SessionSetConfigOptionResult{
            .configOptions = {{.id = "thinking", .name = "Thinking", .type = "boolean", .currentValue = true}},
        };
    }

    template <typename Sink>
    buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
    on_session_prompt(const buraq::v1::SessionPromptParams& p, Sink& sink) {
        using namespace buraq::v1;
        const std::vector<SessionUpdatePayload> all{
            UserMessageChunk{.content = TextContent{.text = "u"}, .messageId = "m0"},
            AgentMessageChunk{.content = TextContent{.text = "a"}, .messageId = "m1"},
            AgentThoughtChunk{.content = TextContent{.text = "t"}},
            ToolCallUpdate{.toolCallId = "tc", .title = "Run", .kind = "execute", .status = "pending",
                           .rawInput = buraq::RawJson{R"({"cmd":["ls","-l"],"n":1})"}},
            ToolCallStatusUpdate{.toolCallId = "tc", .status = "completed",
                                 .rawOutput = buraq::RawJson{R"("ok")"}},
            PlanUpdate{.entries = {{"step", "low", "pending"}}},
            AvailableCommandsUpdate{.availableCommands = {{"web", "Search", AvailableCommandInput{"query"}}}},
            CurrentModeUpdate{.currentModeId = "ask"},
            ConfigOptionUpdate{.configOptions = {{.id = "thinking", .name = "Thinking", .type = "boolean",
                                                   .currentValue = true}}},
            SessionInfoUpdate{.title = "Renamed", .updatedAt = "2025-01-01T00:00:00Z"},
            UsageUpdate{.used = 1200, .size = 200000, .cost = Cost{.amount = 0.5, .currency = "USD"}},
            NoticeUpdate{.severity = "warning", .title = "Rate limited"},
            CompactionUpdate{.compactionId = "cp1", .status = "completed",
                             .summary = std::vector<ContentBlock>{TextContent{.text = "sum"}}},
            CompactionSummaryChunk{.compactionId = "cp1", .content = TextContent{.text = "chunk"}},
        };
        for (const auto& u : all) {
            this->emit_session_update(SessionUpdateNotification{.sessionId = p.sessionId, .update = u}, sink);
        }
        this->request_elicitation(buraq::RequestId{std::int64_t{500}},
            CreateElicitationParams{
                .mode = "form", .message = "Pick a name",
                .requestedSchema = buraq::RawJson{R"({"type":"object","properties":{"name":{"type":"string"}}})"},
                .sessionId = p.sessionId,
            }, sink);
        this->notify_elicitation_complete(CompleteElicitationParams{.elicitationId = "el1"}, sink);
        this->request_ext_method(buraq::RequestId{std::int64_t{501}}, "_vendor/ping",
                                 buraq::RawJson{R"({"x":1})"}, sink);
        this->send_ext_notification("_vendor/note", buraq::RawJson{R"({"y":2})"}, sink);
        this->send_cancel_request(CancelRequestParams{.requestId = buraq::RequestId{std::int64_t{77}}}, sink);
        return SessionPromptResult{.stopReason = "end_turn"};
    }

    void on_cancel_request(const buraq::v1::CancelRequestParams& p) { cancelled_requests.push_back(p.requestId); }

    buraq::expected<buraq::RawJson, buraq::RpcError> on_ext_method(std::string_view method, const buraq::RawJson& p) {
        if (method != "_vendor/echo") {
            return buraq::unexpected(buraq::RpcError::method_not_found(method));
        }
        return p;
    }

    void on_ext_notification(std::string_view method, const buraq::RawJson&) {
        ext_notifications.emplace_back(method);
    }

    void on_raw_response(const buraq::RawEnvelope& env) {
        response_frames.emplace_back(env.result_json);
        responses.push_back(env);
        responses.back().result_json = response_frames.back();
    }
};

template <buraq::JsonCodec Codec>
class StableClient : public buraq::ClientBase<StableClient<Codec>, Codec> {
public:
    std::vector<buraq::v1::SessionUpdatePayload> updates{};
    std::optional<buraq::v1::CreateElicitationParams> elicitation{};
    std::vector<std::string> completed_elicitations{};
    std::vector<std::string> ext_notifications{};
    std::vector<buraq::RequestId> cancelled_requests{};

    void on_session_update(const buraq::v1::SessionUpdateNotification& n) { updates.push_back(n.update); }

    buraq::expected<buraq::v1::CreateElicitationResult, buraq::RpcError>
    on_elicitation_create(const buraq::v1::CreateElicitationParams& p) {
        elicitation = p;
        return buraq::v1::CreateElicitationResult{.action = "accept", .content = buraq::RawJson{R"({"name":"x"})"}};
    }

    void on_elicitation_complete(const buraq::v1::CompleteElicitationParams& p) {
        completed_elicitations.push_back(p.elicitationId);
    }

    buraq::expected<buraq::RawJson, buraq::RpcError> on_ext_method(std::string_view method, const buraq::RawJson& p) {
        CHECK(method == "_vendor/ping");
        return buraq::RawJson{"{\"pong\":" + p.json + "}"};
    }

    void on_ext_notification(std::string_view method, const buraq::RawJson&) {
        ext_notifications.emplace_back(method);
    }

    void on_cancel_request(const buraq::v1::CancelRequestParams& p) { cancelled_requests.push_back(p.requestId); }
};

template <buraq::JsonCodec Codec>
void run_stable_surface_suite() {
    using namespace buraq::v1;
    StableAgent<Codec> agent;
    StableClient<Codec> client;
    std::vector<std::string> to_agent;
    std::vector<std::string> to_client;
    CaptureSink agent_in{to_agent};
    CaptureSink client_in{to_client};
    auto pump = [&]() {
        while (!to_agent.empty() || !to_client.empty()) {
            auto pa = std::move(to_agent);
            to_agent.clear();
            for (const auto& f : pa) { CHECK(agent.feed(as_bytes(f), client_in).has_value()); }
            auto pc = std::move(to_client);
            to_client.clear();
            for (const auto& f : pc) { CHECK(client.feed(as_bytes(f), agent_in).has_value()); }
        }
    };
    auto direct = [&](const std::string& frame) {
        std::vector<std::string> out;
        CaptureSink s{out};
        CHECK(agent.feed(as_bytes(frame), s).has_value());
        CHECK(out.size() == 1);
        return out[0];
    };
    auto error_code_of = [](const std::string& frame) {
        auto env = Codec::parse_envelope(as_bytes(frame));
        CHECK(env.has_value() && env->error.has_value());
        return env->error->code;
    };

    // 1. Pre-initialize gating: -32600
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":1,"method":"session/new","params":{"cwd":"/w","mcpServers":[]}})"))
          == buraq::error_code::invalid_request);

    // 2. initialize: authMethods (agent + terminal), sessionCapabilities with "delete" key
    bool done = false;
    client.send_initialize(InitializeParams{
        .clientCapabilities = ClientCapabilities{
            .session = ClientSessionCapabilities{.configOptions = SessionConfigOptionsCapabilities{.boolean = CapabilityMarker{}}},
            .auth = ClientAuthCapabilities{.terminal = true},
            .elicitation = ElicitationCapabilities{.form = CapabilityMarker{}},
        }}, agent_in,
        [&](buraq::expected<InitializeResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done && agent.initialized());
    CHECK(agent.client_supports_terminal_auth() && agent.client_supports_boolean_config_options());
    CHECK(agent.client_supports_elicitation_form() && !agent.client_supports_elicitation_url());
    CHECK(client.auth_methods().size() == 2);
    CHECK(std::holds_alternative<AuthMethodAgent>(client.auth_methods()[0]));
    CHECK(std::holds_alternative<AuthMethodTerminal>(client.auth_methods()[1]));
    CHECK(std::get<AuthMethodTerminal>(client.auth_methods()[1]).env->at("MODE") == "tty");
    CHECK(client.agent_capabilities() && client.agent_capabilities()->sessionCapabilities);
    CHECK(client.agent_capabilities()->sessionCapabilities->delete_.has_value());
    CHECK(client.agent_capabilities()->auth && client.agent_capabilities()->auth->logout);
    CHECK(client.agent_capabilities()->mcpCapabilities && client.agent_capabilities()->mcpCapabilities->http);
    {
        auto raw = direct(R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":1}})");
        CHECK(raw.find(R"("delete":{})") != std::string::npos);
        CHECK(raw.find(R"("type":"terminal")") != std::string::npos);
        CHECK(raw.find("delete_") == std::string::npos);
    }

    // 3. authenticate / logout
    done = false;
    client.send_authenticate({"cli-login"}, agent_in,
        [&](buraq::expected<AuthenticateResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done && agent.authenticated_with == "cli-login");
    done = false;
    client.send_logout({}, agent_in, [&](buraq::expected<LogoutResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done && agent.logged_out);

    // 4. Validation: missing/relative cwd -> -32602; empty sessionId -> -32602
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":3,"method":"session/new","params":{"mcpServers":[]}})"))
          == buraq::error_code::invalid_params);
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":4,"method":"session/new","params":{"cwd":"rel","mcpServers":[]}})"))
          == buraq::error_code::invalid_params);
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":5,"method":"session/close","params":{"sessionId":""}})"))
          == buraq::error_code::invalid_params);

    // 5. session/new with http/sse/stdio MCP servers, additionalDirectories, systemPrompt, configOptions
    std::string sid;
    client.send_session_new(SessionNewParams{
        .cwd = "/w",
        .mcpServers = {
            McpServerHttp{.name = "h", .url = "https://h", .headers = {{"Authorization", "Bearer x"}}},
            McpServerSse{.name = "s", .url = "https://s"},
            McpServerStdio{.name = "io", .command = "/bin/mcp", .args = {"-v"}, .env = {{"DEBUG", "1"}}},
        },
        .additionalDirectories = std::vector<std::string>{"/srv/shared"},
        .systemPrompt = SystemPrompt{.mode = "append", .content = {TextContent{.text = "be brief"}}},
    }, agent_in, [&](buraq::expected<SessionNewResult, buraq::RpcError> r) {
        CHECK(r.has_value() && r->configOptions && r->configOptions->size() == 2);
        const auto& model = r->configOptions->at(0);
        CHECK(std::get<std::string>(model.currentValue) == "fast");
        CHECK(model.options && model.options->size() == 2);
        CHECK(!model.options->at(0).is_group() && model.options->at(1).is_group());
        CHECK(model.options->at(1).options->at(0).value == "opus");
        CHECK(std::holds_alternative<bool>(r->configOptions->at(1).currentValue));
        sid = r->sessionId;
    });
    pump();
    CHECK(sid == "sess_1");

    // 6. session/set_config_option with a boolean value
    done = false;
    client.send_session_set_config_option({.sessionId = sid, .configId = "thinking", .type = "boolean", .value = true},
        agent_in, [&](buraq::expected<SessionSetConfigOptionResult, buraq::RpcError> r) {
            CHECK(r.has_value() && std::get<bool>(r->configOptions.at(0).currentValue));
            done = true;
        });
    pump();
    CHECK(done);

    // 7. list / resume / close / delete, and session_not_found (-32002 + data.sessionId)
    done = false;
    client.send_session_list({.cwd = "/w"}, agent_in, [&](buraq::expected<SessionListResult, buraq::RpcError> r) {
        CHECK(r.has_value() && r->sessions.size() == 1 && r->sessions[0].title == "T sess_1");
        CHECK(r->nextCursor == "c2");
        done = true;
    });
    pump();
    CHECK(done);
    done = false;
    client.send_session_resume({.sessionId = sid, .cwd = "/w"}, agent_in,
        [&](buraq::expected<SessionResumeResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done);
    done = false;
    client.send_session_close({"nope"}, agent_in, [&](buraq::expected<SessionCloseResult, buraq::RpcError> r) {
        CHECK(!r.has_value() && r.error().code == buraq::error_code::resource_not_found);
        CHECK(r.error().data_json && r.error().data_json->find(R"("sessionId")") != std::string::npos);
        CHECK(r.error().data_json->find("nope") != std::string::npos);
        done = true;
    });
    pump();
    CHECK(done);
    done = false;
    client.send_session_close({sid}, agent_in,
        [&](buraq::expected<SessionCloseResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done);

    // 8. Prompt: all 14 sessionUpdate kinds, elicitation round-trip, ext methods/notifications, $/cancel_request
    done = false;
    client.send_session_prompt({.sessionId = sid, .prompt = {TextContent{.text = "go"}}}, agent_in,
        [&](buraq::expected<SessionPromptResult, buraq::RpcError> r) {
            CHECK(r.has_value() && r->stopReason == "end_turn");
            done = true;
        });
    pump();
    CHECK(done);
    CHECK(client.updates.size() == std::variant_size_v<SessionUpdatePayload>);
    for (std::size_t i = 0; i < client.updates.size(); ++i) {
        CHECK(client.updates[i].index() == i);
    }
    {
        const auto& tc = std::get<ToolCallUpdate>(client.updates[3]);
        CHECK(tc.rawInput && tc.rawInput->json.find(R"("ls")") != std::string::npos);
        const auto& tcu = std::get<ToolCallStatusUpdate>(client.updates[4]);
        CHECK(tcu.rawOutput && tcu.rawOutput->json == R"("ok")");
        CHECK(std::get<AgentMessageChunk>(client.updates[1]).messageId == "m1");
        CHECK(std::get<AvailableCommandsUpdate>(client.updates[6]).availableCommands.at(0).input->hint == "query");
        const auto& usage = std::get<UsageUpdate>(client.updates[10]);
        CHECK(usage.used == 1200 && usage.size == 200000 && usage.cost && usage.cost->currency == "USD");
        CHECK(std::get<NoticeUpdate>(client.updates[11]).severity == "warning");
        CHECK(std::get<CompactionUpdate>(client.updates[12]).summary->size() == 1);
        CHECK(std::get<CompactionSummaryChunk>(client.updates[13]).compactionId == "cp1");
    }
    CHECK(client.elicitation && client.elicitation->mode == "form" && client.elicitation->sessionId == sid);
    CHECK(client.elicitation->requestedSchema && client.elicitation->requestedSchema->json.find("properties") != std::string::npos);
    CHECK(client.completed_elicitations == std::vector<std::string>{"el1"});
    CHECK(client.ext_notifications == std::vector<std::string>{"_vendor/note"});
    CHECK(client.cancelled_requests.size() == 1 && std::get<std::int64_t>(client.cancelled_requests[0]) == 77);
    {
        bool saw_elicitation = false;
        bool saw_ext = false;
        for (const auto& r : agent.responses) {
            CHECK(!r.error.has_value());
            if (r.id && std::get<std::int64_t>(*r.id) == 500) {
                auto res = Codec::template decode<CreateElicitationResult>(r.result_json);
                CHECK(res && res->action == "accept" && res->content && res->content->json.find("x") != std::string::npos);
                saw_elicitation = true;
            }
            if (r.id && std::get<std::int64_t>(*r.id) == 501) {
                CHECK(r.result_json.find("pong") != std::string_view::npos);
                saw_ext = true;
            }
        }
        CHECK(saw_elicitation && saw_ext);
    }

    // 9. Client -> agent ext method (handled / unknown), ext notification, $/cancel_request
    done = false;
    client.send_ext_method("_vendor/echo", buraq::RawJson{R"({"a":[1,2]})"}, agent_in,
        [&](buraq::expected<buraq::RawJson, buraq::RpcError> r) {
            CHECK(r.has_value() && r->json.find("[1,2]") != std::string::npos);
            done = true;
        });
    pump();
    CHECK(done);
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":9,"method":"_vendor/unknown","params":{}})"))
          == buraq::error_code::method_not_found);
    client.send_ext_notification("_vendor/hello", buraq::RawJson{"{}"}, agent_in);
    client.send_cancel_request({.requestId = buraq::RequestId{std::string("r-1")}}, agent_in);
    pump();
    CHECK(agent.ext_notifications == std::vector<std::string>{"_vendor/hello"});
    CHECK(agent.cancelled_requests.size() == 1 && std::get<std::string>(agent.cancelled_requests[0]) == "r-1");

    // 10. delete, then unknown session on resume
    done = false;
    client.send_session_delete({sid}, agent_in,
        [&](buraq::expected<SessionDeleteResult, buraq::RpcError> r) { CHECK(r.has_value()); done = true; });
    pump();
    CHECK(done && agent.sessions.empty());
    CHECK(error_code_of(direct(R"({"jsonrpc":"2.0","id":10,"method":"session/resume","params":{"sessionId":"sess_1","cwd":"/w"}})"))
          == buraq::error_code::resource_not_found);
}

// Frames encoded by one codec must decode identically with the other.
template <typename Enc, typename Dec, typename T>
void check_cross_codec(const T& value) {
    const auto frame = Enc::encode_notification("x", value);
    auto env = Dec::parse_envelope(as_bytes(frame));
    CHECK(env.has_value());
    auto decoded = Dec::template decode<T>(env->params_json);
    CHECK(decoded.has_value());
    CHECK(*decoded == value);
}

template <typename T>
void check_cross_codec_both(const T& value) {
    check_cross_codec<buraq::GlazeCodec, buraq::NlohmannCodec>(value);
    check_cross_codec<buraq::NlohmannCodec, buraq::GlazeCodec>(value);
    check_cross_codec<buraq::GlazeCodec, buraq::GlazeCodec>(value);
    check_cross_codec<buraq::NlohmannCodec, buraq::NlohmannCodec>(value);
}

void run_cross_codec_parity_suite() {
    using namespace buraq::v1;
    std::cout << "[RUN ] Cross-codec parity suite...\n";
    check_cross_codec_both(InitializeResult{
        .agentCapabilities = AgentCapabilities{
            .loadSession = true,
            .promptCapabilities = PromptCapabilities{.image = true},
            .sessionCapabilities = SessionCapabilities{.list = CapabilityMarker{}, .delete_ = CapabilityMarker{}},
        },
        .authMethods = {AuthMethodAgent{.id = "a", .name = "A"},
                        AuthMethodTerminal{.id = "t", .name = "T", .args = std::vector<std::string>{"x"}}},
        .agentInfo = ImplementationInfo{"n", "1"},
    });
    check_cross_codec_both(SessionNewParams{
        .cwd = "/w",
        .mcpServers = {McpServerStdio{.name = "s", .command = "/c"}, McpServerHttp{.name = "h", .url = "u"},
                       McpServerSse{.name = "e", .url = "v", .headers = {{"k", "v"}}}},
    });
    // Default (untagged) alternatives must not gain a `type` key on the wire (newer Glaze versions add one).
    for (const auto& wire : {
             buraq::GlazeCodec::encode_notification("x", SessionNewParams{.cwd = "/w", .mcpServers = {McpServerStdio{.name = "s", .command = "/c"}}}),
             buraq::NlohmannCodec::encode_notification("x", SessionNewParams{.cwd = "/w", .mcpServers = {McpServerStdio{.name = "s", .command = "/c"}}}),
             buraq::GlazeCodec::encode_notification("x", InitializeResult{.authMethods = {AuthMethodAgent{.id = "a", .name = "A"}}}),
             buraq::NlohmannCodec::encode_notification("x", InitializeResult{.authMethods = {AuthMethodAgent{.id = "a", .name = "A"}}}),
         }) {
        CHECK(wire.find("\"type\"") == std::string::npos);
    }
    check_cross_codec_both(SessionSetConfigOptionParams{.sessionId = "s", .configId = "c", .value = std::string("v")});
    check_cross_codec_both(SessionSetConfigOptionParams{.sessionId = "s", .configId = "c", .type = "boolean", .value = false});
    const std::vector<SessionUpdatePayload> payloads{
        UserMessageChunk{.content = ImageContent{.data = "AA==", .mimeType = "image/png"}},
        ToolCallUpdate{.toolCallId = "t", .title = "x", .rawInput = buraq::RawJson{R"({"a":1})"}},
        ToolCallStatusUpdate{.toolCallId = "t", .content = std::vector<ToolCallContent>{ToolCallTerminalItem{.terminalId = "term"}}},
        ConfigOptionUpdate{.configOptions = {{.id = "m", .name = "M", .currentValue = std::string("x"),
                                              .options = std::vector<SessionConfigSelectItem>{{.value = "x", .name = "X"}}}}},
        SessionInfoUpdate{.title = "t"},
        UsageUpdate{.used = 1, .size = 2, .cost = Cost{.amount = 0.25, .currency = "EUR"}},
        NoticeUpdate{.title = "n", .description = "d"},
        CompactionUpdate{.compactionId = "c", .error = "boom"},
        CompactionSummaryChunk{.compactionId = "c", .content = TextContent{.text = "s"}},
    };
    for (const auto& p : payloads) {
        check_cross_codec_both(SessionUpdateNotification{.sessionId = "s", .update = p});
    }
    check_cross_codec_both(CreateElicitationParams{.mode = "url", .message = "m", .elicitationId = "e", .url = "https://x",
                                                   .requestId = buraq::RequestId{std::int64_t{3}}});
    check_cross_codec_both(RequestPermissionParams{
        .sessionId = "s",
        .toolCall = PermissionToolCallInfo{.toolCallId = "t", .rawInput = buraq::RawJson{"[1,2]"}},
        .options = {{"o", "Allow", "allow_once"}},
    });
    std::cout << "[PASS] Cross-codec parity suite passed!\n";
}

template <buraq::JsonCodec Codec>
void run_codec_suite(std::string_view codec_name) {
    std::cout << "[RUN ] " << codec_name << " suite...\n";

    FullTestAgent<Codec> agent;
    FullTestClient<Codec> client;

    std::vector<std::string> to_agent;
    std::vector<std::string> to_client;
    CaptureSink agent_in{to_agent};
    CaptureSink client_in{to_client};

    auto pump = [&]() {
        while (!to_agent.empty() || !to_client.empty()) {
            auto pending_agent = std::move(to_agent);
            to_agent.clear();
            for (const auto& frame : pending_agent) {
                auto status = agent.feed(as_bytes(frame), client_in);
                CHECK(status.has_value());
            }

            auto pending_client = std::move(to_client);
            to_client.clear();
            for (const auto& frame : pending_client) {
                auto status = client.feed(as_bytes(frame), agent_in);
                CHECK(status.has_value());
            }
        }
    };

    // 0. Requests before `initialize` are rejected with -32600 (typed callback receives the error)
    bool callback_invoked = false;
    client.send_session_set_mode({"sess_42", "code"}, agent_in,
        [&](buraq::expected<buraq::v1::SessionSetModeResult, buraq::RpcError> res) {
            CHECK(!res.has_value() && res.error().code == buraq::error_code::invalid_request);
            callback_invoked = true;
        });
    CHECK(client.pending_count() == 1);
    pump();
    CHECK(callback_invoked && client.pending_count() == 0);
    CHECK(!agent.initialized());

    // 1. Handshake: initialize
    const auto init_id = client.send_initialize(buraq::v1::InitializeParams{
        .clientCapabilities = buraq::v1::ClientCapabilities{
            .fs = buraq::v1::ClientCapabilities::FsCapabilities{
                .readTextFile = true,
                .writeTextFile = true,
            },
            .terminal = true,
        },
        .clientInfo = buraq::v1::ImplementationInfo{"buraq-test-client", "1.0.0"},
    }, agent_in);
    pump();
    CHECK(client.response_ids.size() == 1 && client.response_ids.back() == init_id);
    CHECK(agent.client_can_read_text_file() && agent.client_can_write_text_file());
    CHECK(agent.client_supports_terminal());
    CHECK(agent.initialized());

    callback_invoked = false;
    client.send_session_set_mode({"sess_42", "code"}, agent_in,
        [&](buraq::expected<buraq::v1::SessionSetModeResult, buraq::RpcError> res) {
            CHECK(res.has_value());
            callback_invoked = true;
        });
    pump();
    CHECK(callback_invoked && client.pending_count() == 0 && agent.last_mode == "code");

    // 2. Session creation, mode/model switching, and session load
    const auto new_id = client.send_session_new(buraq::v1::SessionNewParams{
        .cwd = "/workspace/buraq",
        .mcpServers = {buraq::v1::McpServerStdio{"git", "uvx", {"mcp-server-git"}, {{"GIT_DIR", ".git"}}}},
    }, agent_in);
    pump();
    CHECK(client.response_ids.back() == new_id);

    client.send_session_set_mode({"sess_42", "ask"}, agent_in);
    client.send_session_set_model({"sess_42", "claude-sonnet"}, agent_in);
    client.send_session_load({"sess_42", "/workspace/buraq", {}}, agent_in);
    pump();
    CHECK(agent.last_mode == "ask");
    CHECK(agent.last_model == "claude-sonnet");

    // 3. Multimodal prompt + streaming SessionUpdatePayload variants
    client.send_session_prompt(buraq::v1::SessionPromptParams{
        .sessionId = "sess_42",
        .prompt = {
            buraq::v1::TextContent{.text = "Analyze this screenshot and file"},
            buraq::v1::ImageContent{.data = "iVBORw0KGgo=", .mimeType = "image/png"},
            buraq::v1::ResourceLinkContent{.uri = "file:///workspace/buraq/README.md", .name = "README.md"},
        },
    }, agent_in);
    pump();

    CHECK(client.updates.size() == 4);
    CHECK(std::holds_alternative<buraq::v1::PlanUpdate>(client.updates[0].update));
    CHECK(std::holds_alternative<buraq::v1::AgentThoughtChunk>(client.updates[1].update));
    CHECK(std::holds_alternative<buraq::v1::ToolCallUpdate>(client.updates[2].update));
    CHECK(std::holds_alternative<buraq::v1::AgentMessageChunk>(client.updates[3].update));

    const auto& tool_call = std::get<buraq::v1::ToolCallUpdate>(client.updates[2].update);
    CHECK(tool_call.content.has_value() && tool_call.content->size() == 1);
    CHECK(std::holds_alternative<buraq::v1::ToolCallDiffItem>((*tool_call.content)[0]));

    // 4. Bidirectional Agent -> Client delegated requests (Permission, FS, Terminal)
    agent.request_permission(buraq::RequestId{"perm_1"}, buraq::v1::RequestPermissionParams{
        .sessionId = "sess_42",
        .toolCall = {.toolCallId = "tc_2", .title = std::string("Run CMake build")},
        .options = {
            {"allow_once", "Allow once", "allow_once"},
            {"reject_once", "Reject", "reject_once"},
        },
    }, client_in);

    agent.request_fs_read(buraq::RequestId{"fs_1"}, buraq::v1::FsReadTextFileParams{
        .sessionId = "sess_42",
        .path = "/workspace/buraq/include/buraq/acp.hpp",
    }, client_in);

    agent.request_terminal_create(buraq::RequestId{"term_1"}, buraq::v1::TerminalCreateParams{
        .sessionId = "sess_42",
        .command = "cmake",
        .args = {"--build", "build"},
    }, client_in);
    pump();

    CHECK(client.permission_requested);
    CHECK(client.terminal_created);
    CHECK(agent.received_responses.size() == 3);

    // 5. Cancellation notification
    client.send_session_cancel({"sess_42"}, agent_in);
    pump();
    CHECK(agent.cancel_received);

    // 6. Compile-time unimplemented method fallback (-32601 Method not found)
    MinimalAgent<Codec> minimal_agent;
    std::vector<std::string> err_frames;
    CaptureSink err_sink{err_frames};
    const std::string minimal_init =
        R"({"jsonrpc":"2.0","id":98,"method":"initialize","params":{"protocolVersion":1}})";
    CHECK(minimal_agent.feed(as_bytes(minimal_init), err_sink).has_value());
    err_frames.clear();

    const std::string unsupported_req =
        R"({"jsonrpc":"2.0","id":99,"method":"session/load","params":{"sessionId":"s1","cwd":"/tmp","mcpServers":[]}})";
    auto feed_res = minimal_agent.feed(as_bytes(unsupported_req), err_sink);
    CHECK(feed_res.has_value());
    CHECK(err_frames.size() == 1);

    auto err_env = Codec::parse_envelope(as_bytes(err_frames[0]));
    CHECK(err_env.has_value() && err_env->error.has_value());
    CHECK(err_env->error->code == -32601);

    // 7. Malformed JSON error propagation via expected<void, RpcError>
    const std::string bad_json = R"({"jsonrpc":"2.0", broken)";
    auto bad_res = minimal_agent.feed(as_bytes(bad_json), err_sink);
    CHECK(!bad_res.has_value());
    CHECK(bad_res.error().code == -32700);

    run_protocol_hardening_suite<Codec>();
    run_stable_surface_suite<Codec>();

    std::cout << "[PASS] " << codec_name << " suite passed!\n";
}

} // namespace

int main() {
    run_codec_suite<buraq::GlazeCodec>("GlazeCodec");
    run_codec_suite<buraq::NlohmannCodec>("NlohmannCodec");
    run_cross_codec_parity_suite();
    std::cout << "All buraq ACP v1 dispatch & codec parity tests passed.\n";
    return 0;
}