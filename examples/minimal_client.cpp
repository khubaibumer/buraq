// Minimal ACP client: launches an agent, opens a session, sends one prompt and prints the streamed reply.
//
//   buraq_minimal_client [agent-command [args...]]     (default: ./buraq_echo_agent)
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/runtime/connection.hpp>

class MinimalClient : public buraq::ClientBase<MinimalClient, buraq::GlazeCodec> {
public:
    // Streamed agent output (runs on the connection's dispatcher thread)
    void on_session_update(const buraq::v1::SessionUpdateNotification &notif) {
        if (const auto *msg = std::get_if<buraq::v1::AgentMessageChunk>(&notif.update)) {
            if (const auto *text = std::get_if<buraq::v1::TextContent>(&msg->content)) {
                std::cout << "[Agent] " << text->text << '\n';
            }
        } else if (const auto *thought = std::get_if<buraq::v1::AgentThoughtChunk>(&notif.update)) {
            if (const auto *text = std::get_if<buraq::v1::TextContent>(&thought->content)) {
                std::cout << "[Thought] " << text->text << '\n';
            }
        }
    }

    // Auto-approve tool calls with the first option offered
    buraq::expected<buraq::v1::RequestPermissionResult, buraq::RpcError>
    on_request_permission(const buraq::v1::RequestPermissionParams &params) {
        if (params.options.empty()) {
            return buraq::v1::RequestPermissionResult{.outcome = buraq::v1::PermissionCancelledOutcome{}};
        }
        return buraq::v1::RequestPermissionResult{
            .outcome = buraq::v1::PermissionSelectedOutcome{.optionId = params.options.front().optionId}
        };
    }

    buraq::expected<buraq::v1::FsReadTextFileResult, buraq::RpcError>
    on_fs_read_text_file(const buraq::v1::FsReadTextFileParams &params) {
        std::cout << "[Client FS] Agent requested file: " << params.path << '\n';
        return buraq::v1::FsReadTextFileResult{.content = "// sample file content"};
    }
};

int main(int argc, char *argv[]) {
    std::vector<std::string> command{argv + 1, argv + argc};
    if (command.empty()) command.emplace_back("./buraq_echo_agent");

    MinimalClient client;
    auto conn = buraq::runtime::spawn_agent(client, command);
    if (!conn) {
        std::cerr << "[Client] " << conn.error() << '\n';
        return 1;
    }
    auto &agent = **conn;

    auto init = agent.initialize({
        .clientCapabilities = {.fs = buraq::v1::FileSystemCapabilities{.readTextFile = true}},
        .clientInfo = buraq::v1::ImplementationInfo{"buraq-minimal-client", "0.1.0"},
    }).get();
    if (!init) {
        std::cerr << "[Client] initialize failed: " << init.error().message << '\n';
        return 1;
    }
    std::cout << "[Client] Connected to agent: " << (init->agentInfo ? init->agentInfo->name : "unknown") << '\n';

    auto session = agent.new_session({.cwd = "/tmp"}).get();
    if (!session) {
        std::cerr << "[Client] session/new failed: " << session.error().message << '\n';
        return 1;
    }
    std::cout << "[Client] Created session: " << session->sessionId << '\n';

    auto turn = agent.prompt({
        .sessionId = session->sessionId,
        .prompt = {buraq::v1::TextContent{.text = "Hello from minimal_client!"}},
    }).get();
    if (!turn) {
        std::cerr << "[Client] session/prompt failed: " << turn.error().message << '\n';
        return 1;
    }
    std::cout << "[Client] Turn finished: " << turn->stopReason << '\n';
    return 0; // the connection closes the agent's stdin and reaps it
}
