// Tests for buraq::runtime (Connection, spawn_agent) and buraq::transport::Process.
//
//   buraq_runtime_test [path-to-buraq_echo_agent]
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/codecs/nlohmann.hpp>
#include <buraq/runtime/connection.hpp>

#define CHECK(expr)                                                                         \
    do {                                                                                    \
        if (!(expr)) {                                                                      \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expr << '\n';  \
            std::abort();                                                                   \
        }                                                                                   \
    } while (false)

using namespace std::chrono_literals;
namespace v1 = buraq::v1;

namespace {
    // One direction of an in-memory line pipe.
    struct LineQueue {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::string> lines;
        bool closed{false};
    };

    // LineChannel over two LineQueues, so two Connections can talk in-process.
    class QueueChannel final : public buraq::runtime::LineChannel {
    public:
        QueueChannel(std::shared_ptr<LineQueue> in, std::shared_ptr<LineQueue> out) : in_(std::move(in)),
            out_(std::move(out)) {
        }

        bool read_line(std::string &line) override {
            std::unique_lock lock(in_->mutex);
            in_->cv.wait(lock, [&] { return in_->closed || !in_->lines.empty(); });
            if (in_->lines.empty()) return false;
            line = std::move(in_->lines.front());
            in_->lines.pop_front();
            return true;
        }

        bool write_line(std::string_view frame) override {
            std::lock_guard lock(out_->mutex);
            if (out_->closed) return false;
            out_->lines.emplace_back(frame);
            out_->cv.notify_all();
            return true;
        }

        void close_output() override { close(*out_); }

        void abort() override {
            close(*in_);
            close(*out_);
        }

    private:
        static void close(LineQueue &q) {
            std::lock_guard lock(q.mutex);
            q.closed = true;
            q.cv.notify_all();
        }

        std::shared_ptr<LineQueue> in_;
        std::shared_ptr<LineQueue> out_;
    };

    std::pair<std::shared_ptr<QueueChannel>, std::shared_ptr<QueueChannel> > channel_pair() {
        auto a = std::make_shared<LineQueue>();
        auto b = std::make_shared<LineQueue>();
        return {std::make_shared<QueueChannel>(a, b), std::make_shared<QueueChannel>(b, a)};
    }

    // Agent whose prompt handler blocks: it asks the client for permission (use_future), streams an update,
    // and then waits for session/cancel when the prompt text is "wait".
    template<typename Codec>
    class BlockingAgent : public buraq::AgentBase<BlockingAgent<Codec>, Codec> {
    public:
        std::mutex mutex;
        std::condition_variable cv;
        bool cancelled{false};

        buraq::expected<v1::InitializeResult, buraq::RpcError> on_initialize(const v1::InitializeParams &) {
            return v1::InitializeResult{.agentInfo = v1::ImplementationInfo{"blocking", "1.0"}};
        }

        buraq::expected<v1::SessionNewResult, buraq::RpcError> on_session_new(const v1::SessionNewParams &) {
            return v1::SessionNewResult{.sessionId = "s1"};
        }

        template<typename Sink>
        buraq::expected<v1::SessionPromptResult, buraq::RpcError> on_session_prompt(
            const v1::SessionPromptParams &params,
            Sink &sink) {
            auto outcome = this->request_permission(
                        v1::RequestPermissionParams{
                            .sessionId = params.sessionId,
                            .toolCall = {.toolCallId = "t1"},
                            .options = {{.optionId = "yes", .name = "Yes", .kind = "allow_once"}},
                        },
                        sink, buraq::use_future)
                    .get();
            if (!outcome) return buraq::unexpected(outcome.error());
            const auto *selected = std::get_if<v1::PermissionSelectedOutcome>(&outcome->outcome);
            std::string text = selected ? "approved:" + selected->optionId : "denied";
            this->emit_session_update(
                v1::SessionUpdateNotification{
                    .sessionId = params.sessionId,
                    .update = v1::AgentMessageChunk{.content = v1::TextContent{.text = text}}
                },
                sink);

            const auto *first = params.prompt.empty() ? nullptr : std::get_if<v1::TextContent>(&params.prompt.front());
            if (first && first->text == "wait") {
                std::unique_lock lock(mutex);
                if (!cv.wait_for(lock, 5s, [&] { return cancelled; }))
                    return v1::SessionPromptResult{
                        .stopReason = "timeout"
                    };
                return v1::SessionPromptResult{.stopReason = "cancelled"};
            }
            return v1::SessionPromptResult{.stopReason = "end_turn"};
        }

        void on_session_cancel(const v1::SessionCancelParams &) {
            std::lock_guard lock(mutex);
            cancelled = true;
            cv.notify_all();
        }
    };

    template<typename Codec>
    class RecordingClient : public buraq::ClientBase<RecordingClient<Codec>, Codec> {
    public:
        std::mutex mutex;
        std::vector<std::string> texts;

        void on_session_update(const v1::SessionUpdateNotification &notif) {
            if (const auto *chunk = std::get_if<v1::AgentMessageChunk>(&notif.update)) {
                if (const auto *text = std::get_if<v1::TextContent>(&chunk->content)) {
                    std::lock_guard lock(mutex);
                    texts.push_back(text->text);
                }
            }
        }

        buraq::expected<v1::RequestPermissionResult, buraq::RpcError>
        on_request_permission(const v1::RequestPermissionParams &params) {
            return v1::RequestPermissionResult{
                .outcome = v1::PermissionSelectedOutcome{.optionId = params.options.at(0).optionId}
            };
        }
    };

    template<typename Codec>
    void run_in_process_suite(buraq::runtime::Dispatch dispatch) {
        BlockingAgent<Codec> agent;
        RecordingClient<Codec> client;
        auto [agent_end, client_end] = channel_pair();
        buraq::runtime::ConnectionOptions opts{.dispatch = dispatch, .on_error = {}, .close_grace = 1s};
        buraq::runtime::Connection agent_conn(agent, agent_end, opts);
        buraq::runtime::Connection client_conn(client, client_end, opts);
        agent_conn.start();
        client_conn.start();

        auto init = client_conn.initialize().get();
        CHECK(init && init->agentInfo && init->agentInfo->name == "blocking");
        auto session = client_conn.new_session({.cwd = "/tmp"}).get();
        CHECK(session && session->sessionId == "s1");

        // The agent's prompt handler blocks on a client reply (agent -> client request inside a request).
        auto turn = client_conn.prompt({.sessionId = "s1", .prompt = {v1::TextContent{.text = "hi"}}}).get();
        CHECK(turn && turn->stopReason == "end_turn");
        {
            // Updates are delivered before the reply that ends the turn.
            std::lock_guard lock(client.mutex);
            CHECK(client.texts.size() == 1 && client.texts[0] == "approved:yes");
        }

        // session/cancel interrupts a prompt that is still running.
        auto pending = client_conn.prompt({.sessionId = "s1", .prompt = {v1::TextContent{.text = "wait"}}});
        CHECK(pending.wait_for(100ms) == std::future_status::timeout);
        client_conn.cancel({.sessionId = "s1"});
        auto cancelled = pending.get();
        CHECK(cancelled && cancelled->stopReason == "cancelled");

        // Generic request for a method the agent does not implement.
        auto ext = client_conn.template request<buraq::RawJson>("_example/unknown", buraq::RawJson{"{}"}).get();
        CHECK(!ext && ext.error().code == buraq::error_code::method_not_found);

        // Closing the client side ends both connections; requests after close fail instead of hanging.
        client_conn.close();
        CHECK(client_conn.finished());
        CHECK(agent_conn.wait_for(2s));
        auto late = client_conn.new_session({.cwd = "/tmp"}).get();
        CHECK(!late);
    }

    // EOF while a request is outstanding fails the request with "Connection closed".
    void run_eof_test() {
        RecordingClient<buraq::GlazeCodec> client;
        auto [peer_end, client_end] = channel_pair();
        buraq::runtime::Connection conn(client, client_end, {.dispatch = {}, .on_error = {}, .close_grace = 200ms});
        conn.start();
        auto fut = conn.initialize();
        std::string line;
        CHECK(peer_end->read_line(line) && line.find("\"initialize\"") != std::string::npos);
        peer_end->abort();
        auto result = fut.get();
        CHECK(!result && result.error().code == buraq::error_code::internal_error);
        CHECK(conn.wait_for(2s));
    }

    void run_spawn_tests(const char *echo_agent) {
        RecordingClient<buraq::GlazeCodec> client;
        auto missing = buraq::runtime::spawn_agent(client, {"/nonexistent/buraq-agent-binary"});
        CHECK(!missing && !missing.error().empty());
        auto empty = buraq::runtime::spawn_agent(client, {});
        CHECK(!empty);

        if (echo_agent == nullptr) return;
        auto conn = buraq::runtime::spawn_agent(client, {echo_agent});
        CHECK(conn);
        auto &agent = **conn;
        auto init = agent.initialize().get();
        CHECK(init && init->agentInfo && init->agentInfo->name == "buraq-echo");
        auto session = agent.new_session({.cwd = "/tmp"}).get();
        CHECK(session);
        auto turn = agent.prompt({.sessionId = session->sessionId, .prompt = {v1::TextContent{.text = "ping"}}}).get();
        CHECK(turn && turn->stopReason == "end_turn");
        {
            std::lock_guard lock(client.mutex);
            CHECK(!client.texts.empty() && client.texts.back() == "ping");
        }
        agent.close();
        CHECK(agent.finished());
        CHECK(!(*conn)->channel().read_line(*std::make_unique<std::string>()));
    }

#ifndef _WIN32
    // buraq::transport::Process directly, using POSIX shell utilities.
    void run_process_tests() {
        using buraq::transport::Process;
        using buraq::transport::SpawnOptions;

        // env (without inheriting), cwd, unterminated final line, exit code.
        auto p = Process::spawn({"/bin/sh", "-c", "printf '%s\\n' \"$BQ_VAR\"; pwd; printf 'tail\\r'; exit 3"},
                                SpawnOptions{
                                    .env = {{"BQ_VAR", "hello"}}, .inherit_env = false, .cwd = "/",
                                    .inherit_stderr = false
                                });
        CHECK(p);
        std::string line;
        CHECK(p->read_line(line) && line == "hello");
        CHECK(p->read_line(line) && line == "/");
        CHECK(p->read_line(line) && line == "tail");
        CHECK(!p->read_line(line));
        CHECK(p->wait() == 3);
        CHECK(!p->running());
        CHECK(p->try_wait() == 3);

        // stdin round trip; closing stdin ends `cat`.
        auto cat = Process::spawn({"cat"});
        CHECK(cat && cat->running() && cat->pid() > 0);
        CHECK(cat->write("ping\n"));
        CHECK(cat->read_line(line) && line == "ping");
        CHECK(!cat->try_wait());
        cat->close_stdin();
        CHECK(!cat->read_line(line));
        CHECK(cat->wait() == 0);

        // terminate(): graceful SIGTERM, then SIGKILL for a process that ignores it.
        auto sleeper = Process::spawn({"sleep", "30"});
        CHECK(sleeper);
        CHECK(sleeper->terminate(1s) != 0);
        CHECK(!sleeper->running());
        auto stubborn = Process::spawn({"/bin/sh", "-c", "trap '' TERM; echo ready; while :; do sleep 1; done"});
        CHECK(stubborn && stubborn->read_line(line) && line == "ready");
        CHECK(stubborn->terminate(100ms) != 0);
        CHECK(!stubborn->running());

        // Moves keep ownership; a missing cwd is reported.
        auto moved = Process::spawn({"true"});
        CHECK(moved);
        Process target = std::move(*moved);
        CHECK(target.wait() == 0);
        auto bad_cwd = Process::spawn({"true"}, SpawnOptions{.cwd = "/nonexistent/buraq-dir"});
        CHECK(!bad_cwd);
    }
#endif
} // namespace

int main(int argc, char *argv[]) {
#ifndef _WIN32
    run_process_tests();
#endif
    run_in_process_suite<buraq::GlazeCodec>(buraq::runtime::Dispatch::serial);
    run_in_process_suite<buraq::GlazeCodec>(buraq::runtime::Dispatch::concurrent);
    run_in_process_suite<buraq::NlohmannCodec>(buraq::runtime::Dispatch::serial);
    run_in_process_suite<buraq::NlohmannCodec>(buraq::runtime::Dispatch::concurrent);
    run_eof_test();
    run_spawn_tests(argc > 1 ? argv[1] : nullptr);
    std::cout << "runtime tests passed\n";
    return 0;
}
