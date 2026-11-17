#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>

class MinimalClient : public buraq::ClientBase<MinimalClient, buraq::GlazeCodec> {
public:
    bool init_done{false};
    std::string session_id{};
    bool turn_done{false};

    void on_session_update(const buraq::v1::SessionUpdateNotification& notif) {
        if (const auto* msg = std::get_if<buraq::v1::AgentMessageChunk>(&notif.update)) {
            if (const auto* text = std::get_if<buraq::v1::TextContent>(&msg->content)) {
                std::cout << "[Agent] " << text->text << '\n';
            }
        } else if (const auto* thought = std::get_if<buraq::v1::AgentThoughtChunk>(&notif.update)) {
            if (const auto* text = std::get_if<buraq::v1::TextContent>(&thought->content)) {
                std::cout << "[Thought] " << text->text << '\n';
            }
        }
    }

    buraq::expected<buraq::v1::FsReadTextFileResult, buraq::RpcError>
    on_fs_read_text_file(const buraq::v1::FsReadTextFileParams& params) {
        std::cout << "[Client FS] Agent requested file: " << params.path << '\n';
        return buraq::v1::FsReadTextFileResult{.content = "// sample file content"};
    }
};

int main(int argc, char* argv[]) {
    const char* agent_bin = (argc > 1) ? argv[1] : "./buraq_echo_agent";

    int to_agent[2];
    int from_agent[2];
    if (pipe(to_agent) != 0 || pipe(from_agent) != 0) {
        std::perror("pipe");
        return 1;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        return 1;
    }

    if (pid == 0) {
        dup2(to_agent[0], STDIN_FILENO);
        dup2(from_agent[1], STDOUT_FILENO);
        close(to_agent[0]);
        close(to_agent[1]);
        close(from_agent[0]);
        close(from_agent[1]);

        execl(agent_bin, agent_bin, nullptr);
        std::perror("execl");
        std::_Exit(127);
    }

    close(to_agent[0]);
    close(from_agent[1]);

    FILE* agent_out = fdopen(from_agent[0], "r");
    auto pipe_sink = [fd = to_agent[1]](std::span<const std::byte> bytes) {
        (void)write(fd, bytes.data(), bytes.size());
        if (bytes.empty() || bytes.back() != std::byte{'\n'}) {
            (void)write(fd, "\n", 1);
        }
    };

    auto read_next_frame = [&](MinimalClient& client) -> bool {
        char buf[8192];
        if (!std::fgets(buf, sizeof(buf), agent_out)) return false;
        std::string_view line{buf};
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.remove_suffix(1);
        }
        if (line.empty()) return true;
        auto status = client.feed(std::as_bytes(std::span{line.data(), line.size()}), pipe_sink);
        if (!status) {
            std::cerr << "[Client] Protocol error: " << status.error().message << '\n';
            return false;
        }
        return true;
    };

    MinimalClient client;

    // 1. initialize
    client.send_initialize(buraq::v1::InitializeParams{
        .clientCapabilities = {.fs = buraq::v1::ClientCapabilities::FsCapabilities{.readTextFile = true}},
        .clientInfo = buraq::v1::ImplementationInfo{"buraq-minimal-client", "0.1.0"},
    }, pipe_sink, [&](buraq::expected<buraq::v1::InitializeResult, buraq::RpcError> res) {
        if (res) {
            std::cout << "[Client] Connected to agent: "
                      << (res->agentInfo ? res->agentInfo->name : "unknown") << '\n';
            client.init_done = true;
        } else {
            std::cerr << "[Client] initialize failed: " << res.error().message << '\n';
        }
    });
    while (!client.init_done && read_next_frame(client)) {}

    // 2. session/new
    client.send_session_new(buraq::v1::SessionNewParams{.cwd = "/tmp"}, pipe_sink,
        [&](buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError> res) {
            if (res) {
                client.session_id = res->sessionId;
                std::cout << "[Client] Created session: " << client.session_id << '\n';
            } else {
                std::cerr << "[Client] session/new failed: " << res.error().message << '\n';
            }
        });
    while (client.session_id.empty() && read_next_frame(client)) {}

    // 3. session/prompt
    client.send_session_prompt(buraq::v1::SessionPromptParams{
        .sessionId = client.session_id,
        .prompt = {buraq::v1::TextContent{.text = "Hello from minimal_client!"}},
    }, pipe_sink, [&](buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError> res) {
        if (res) {
            std::cout << "[Client] Turn finished: " << res->stopReason << '\n';
            client.turn_done = true;
        } else {
            std::cerr << "[Client] session/prompt failed: " << res.error().message << '\n';
        }
    });
    while (!client.turn_done && read_next_frame(client)) {}

    close(to_agent[1]);
    std::fclose(agent_out);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}