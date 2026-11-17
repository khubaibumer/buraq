#include <cassert>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/codecs/nlohmann.hpp>

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
        assert(params.protocolVersion == buraq::v1::kProtocolVersion);
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
        assert(params.cwd == "/workspace/buraq");
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
    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams&) {
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
        assert(!params.options.empty());
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
        assert(params.command == "cmake");
        return buraq::v1::TerminalCreateResult{.terminalId = "term_99"};
    }

    void on_raw_response(const buraq::RawEnvelope& env) {
        assert(!env.error.has_value());
        if (env.id && std::holds_alternative<std::int64_t>(*env.id)) {
            response_ids.push_back(std::get<std::int64_t>(*env.id));
        }
    }
};

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
                assert(status.has_value());
            }

            auto pending_client = std::move(to_client);
            to_client.clear();
            for (const auto& frame : pending_client) {
                auto status = client.feed(as_bytes(frame), agent_in);
                assert(status.has_value());
            }
        }
    };

    bool callback_invoked = false;
    client.send_session_set_mode({"sess_42", "code"}, agent_in,
        [&](buraq::expected<buraq::v1::SessionSetModeResult, buraq::RpcError> res) {
            assert(res.has_value());
            callback_invoked = true;
        });
    assert(client.pending_count() == 1);
    pump();
    assert(callback_invoked && client.pending_count() == 0);

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
    assert(client.response_ids.size() == 1 && client.response_ids.back() == init_id);

    // 2. Session creation, mode/model switching, and session load
    const auto new_id = client.send_session_new(buraq::v1::SessionNewParams{
        .cwd = "/workspace/buraq",
        .mcpServers = {{"git", "uvx", {"mcp-server-git"}, {{"GIT_DIR", ".git"}}}},
    }, agent_in);
    pump();
    assert(client.response_ids.back() == new_id);

    client.send_session_set_mode({"sess_42", "ask"}, agent_in);
    client.send_session_set_model({"sess_42", "claude-sonnet"}, agent_in);
    client.send_session_load({"sess_42", "/workspace/buraq", {}}, agent_in);
    pump();
    assert(agent.last_mode == "ask");
    assert(agent.last_model == "claude-sonnet");

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

    assert(client.updates.size() == 4);
    assert(std::holds_alternative<buraq::v1::PlanUpdate>(client.updates[0].update));
    assert(std::holds_alternative<buraq::v1::AgentThoughtChunk>(client.updates[1].update));
    assert(std::holds_alternative<buraq::v1::ToolCallUpdate>(client.updates[2].update));
    assert(std::holds_alternative<buraq::v1::AgentMessageChunk>(client.updates[3].update));

    const auto& tool_call = std::get<buraq::v1::ToolCallUpdate>(client.updates[2].update);
    assert(tool_call.content.has_value() && tool_call.content->size() == 1);
    assert(std::holds_alternative<buraq::v1::ToolCallDiffItem>((*tool_call.content)[0]));

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
        .path = "include/buraq/acp.hpp",
    }, client_in);

    agent.request_terminal_create(buraq::RequestId{"term_1"}, buraq::v1::TerminalCreateParams{
        .sessionId = "sess_42",
        .command = "cmake",
        .args = {"--build", "build"},
    }, client_in);
    pump();

    assert(client.permission_requested);
    assert(client.terminal_created);
    assert(agent.received_responses.size() == 3);

    // 5. Cancellation notification
    client.send_session_cancel({"sess_42"}, agent_in);
    pump();
    assert(agent.cancel_received);

    // 6. Compile-time unimplemented method fallback (-32601 Method not found)
    MinimalAgent<Codec> minimal_agent;
    std::vector<std::string> err_frames;
    CaptureSink err_sink{err_frames};

    const std::string unsupported_req =
        R"({"jsonrpc":"2.0","id":99,"method":"session/load","params":{"sessionId":"s1","cwd":"/tmp","mcpServers":[]}})";
    auto feed_res = minimal_agent.feed(as_bytes(unsupported_req), err_sink);
    assert(feed_res.has_value());
    assert(err_frames.size() == 1);

    auto err_env = Codec::parse_envelope(as_bytes(err_frames[0]));
    assert(err_env.has_value() && err_env->error.has_value());
    assert(err_env->error->code == -32601);

    // 7. Malformed JSON error propagation via expected<void, RpcError>
    const std::string bad_json = R"({"jsonrpc":"2.0", broken)";
    auto bad_res = minimal_agent.feed(as_bytes(bad_json), err_sink);
    assert(!bad_res.has_value());
    assert(bad_res.error().code == -32700);

    std::cout << "[PASS] " << codec_name << " suite passed!\n";
}

} // namespace

int main() {
    run_codec_suite<buraq::GlazeCodec>("GlazeCodec");
    run_codec_suite<buraq::NlohmannCodec>("NlohmannCodec");
    std::cout << "All buraq ACP v1 dispatch & codec parity tests passed.\n";
    return 0;
}