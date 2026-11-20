/// @file
/// @brief Batteries-included threaded runtime: run an agent on stdio or spawn one from a client in a few lines.
///
/// Opt-in header (not included by buraq/acp.hpp). The core of buraq is sans-I/O (`feed()` + sinks); this
/// header adds the plumbing most applications need on top of it:
///
/// - buraq::runtime::Connection: owns a reader thread and a dispatcher thread for one peer (agent or client),
///   with a thread-safe outbound sink, so handlers may block on `buraq::use_future` replies from the other side.
/// - buraq::runtime::serve_stdio(): serve an agent over stdin/stdout.
/// - buraq::runtime::spawn_agent(): launch an agent subprocess and connect a client to it.
///
/// @code
/// // Agent
/// int main() { MyAgent agent; return buraq::runtime::serve_stdio(agent); }
///
/// // Client
/// MyClient client;
/// auto conn = buraq::runtime::spawn_agent(client, {"my-agent", "--acp"}).value();
/// auto init = conn->initialize({.clientInfo = buraq::v1::ImplementationInfo{"my-editor", "1.0"}}).get();
/// auto session = conn->new_session({.cwd = "/work"}).get();
/// auto turn = conn->prompt({.sessionId = session->sessionId,
///                           .prompt = {buraq::v1::TextContent{.text = "Hi"}}}).get();
/// @endcode
///
/// Threading model:
/// - Notifications (`session/update`, `session/cancel`, ...) and responses are processed in arrival order on
///   the reader thread, so every update of a turn is delivered before the turn's reply completes its future.
///   Notification hooks and callback continuations must therefore not block on another reply.
/// - Requests are processed in arrival order on a single dispatcher thread (Dispatch::serial, default), so
///   request hooks may block on `use_future` replies. With Dispatch::concurrent each request runs on its own
///   thread instead.
/// - Request hooks can thus run concurrently with notification hooks (that is what lets `session/cancel`
///   interrupt a running `session/prompt`): synchronize state shared between them.
/// - Do not call Connection::close() (or destroy the connection) from inside a hook.
#pragma once

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "buraq/core/agent_base.hpp"
#include "buraq/core/async.hpp"
#include "buraq/core/client_base.hpp"
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"
#include "buraq/transport/process.hpp"

namespace buraq::runtime {

/// A bidirectional, newline-delimited byte channel (one JSON-RPC message per line).
class LineChannel {
public:
    virtual ~LineChannel() = default;
    /// Reads one non-empty line (blocking), without its terminator.
    /// @param line Receives the line.
    /// @return `false` at end of stream.
    virtual bool read_line(std::string& line) = 0;
    /// Writes @p frame followed by a newline and flushes.
    /// @param frame One serialized message.
    /// @return `false` if the channel is closed or broken.
    virtual bool write_line(std::string_view frame) = 0;
    /// Signals end of output to the peer (e.g. closes the child's stdin). Must be safe to call repeatedly.
    virtual void close_output() {}
    /// Forcibly unblocks a pending read_line() (e.g. by terminating the child). Called by Connection::close()
    /// when the peer does not end the stream after close_output().
    virtual void abort() {}
};

/// LineChannel over a pair of standard streams (e.g. std::cin / std::cout).
class StreamChannel final : public LineChannel {
public:
    /// @param in Input stream.
    /// @param out Output stream.
    StreamChannel(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

    bool read_line(std::string& line) override {
        while (std::getline(in_, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) return true;
        }
        return false;
    }

    bool write_line(std::string_view frame) override {
        out_.write(frame.data(), static_cast<std::streamsize>(frame.size()));
        if (frame.empty() || frame.back() != '\n') out_.put('\n');
        out_.flush();
        return static_cast<bool>(out_);
    }

private:
    std::istream& in_;
    std::ostream& out_;
};

/// LineChannel over a child process's stdin/stdout.
class ProcessChannel final : public LineChannel {
public:
    /// @param process A running process (ownership is taken).
    explicit ProcessChannel(transport::Process process) : process_(std::move(process)) {}

    bool read_line(std::string& line) override {
        while (process_.read_line(line)) {
            if (!line.empty()) return true;
        }
        return false;
    }

    bool write_line(std::string_view frame) override {
        if (!process_.write(frame)) return false;
        return (!frame.empty() && frame.back() == '\n') || process_.write("\n");
    }

    void close_output() override { process_.close_stdin(); }
    void abort() override { process_.terminate(std::chrono::milliseconds(500)); }

    /// The underlying process (e.g. to query its exit code after the connection ended).
    transport::Process& process() noexcept { return process_; }

private:
    transport::Process process_;
};

/// How a Connection runs inbound requests.
enum class Dispatch {
    /// One dispatcher thread, arrival order; request hooks never run concurrently with each other (default).
    serial,
    /// Each request on its own thread.
    concurrent,
};

/// Connection tuning knobs.
struct ConnectionOptions {
    /// Request dispatch policy.
    Dispatch dispatch{Dispatch::serial};
    /// Receives protocol and handler errors that cannot be reported to the peer; logs to std::cerr if empty.
    std::function<void(std::string_view)> on_error{};
    /// Time close() waits for the peer to end its output before calling LineChannel::abort().
    std::chrono::milliseconds close_grace{std::chrono::milliseconds(2000)};
};

namespace detail {

/// Serializes writes to a channel and lets late writers (e.g. a Responder outliving the connection) no-op.
class Writer {
public:
    /// @param channel Transport written to.
    explicit Writer(std::shared_ptr<LineChannel> channel) : channel_(std::move(channel)) {}

    /// Writes one frame unless closed. @return `false` if the writer is (or just became) closed.
    bool write(std::span<const std::byte> bytes) {
        const std::string_view frame{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        std::lock_guard lock(mutex_);
        if (!open_) return false;
        if (!channel_->write_line(frame)) {
            open_ = false;
        }
        return open_;
    }

    /// @return `true` until close() or a failed write.
    bool is_open() {
        std::lock_guard lock(mutex_);
        return open_;
    }

    /// Drops every subsequent write and closes the channel's output once (the peer then sees end of stream).
    void close() {
        std::lock_guard lock(mutex_);
        open_ = false;
        if (!output_closed_) {
            output_closed_ = true;
            channel_->close_output();
        }
    }

private:
    std::mutex mutex_;
    std::shared_ptr<LineChannel> channel_;
    bool open_{true};
    bool output_closed_{false};
};

} // namespace detail

/// Thread-safe, copyable outbound sink of a Connection (usable with every `feed`/`emit_*`/`send_*` API).
///
/// Writes after the connection closed are silently dropped.
class ConnectionSink {
public:
    /// @param writer Shared writer of the connection.
    explicit ConnectionSink(std::shared_ptr<detail::Writer> writer) : writer_(std::move(writer)) {}
    /// Writes one frame (a newline is appended).
    /// @param bytes Serialized message.
    void operator()(std::span<const std::byte> bytes) const { writer_->write(bytes); }

private:
    std::shared_ptr<detail::Writer> writer_;
};

/// Satisfied by buraq::AgentBase-derived classes.
template <typename Peer>
concept AgentPeer = requires { typename Peer::codec_type; } &&
                    std::derived_from<Peer, AgentBase<Peer, typename Peer::codec_type>>;

/// Satisfied by buraq::ClientBase-derived classes.
template <typename Peer>
concept ClientPeer = requires { typename Peer::codec_type; } &&
                     std::derived_from<Peer, ClientBase<Peer, typename Peer::codec_type>>;

/// Runs one ACP peer (agent or client) over a LineChannel with background threads.
///
/// Not copyable or movable (threads refer to it); spawn_agent() returns it in a `std::unique_ptr`.
/// The peer must outlive the connection. See the file documentation for the threading model.
/// @tparam Peer An AgentBase- or ClientBase-derived class.
template <typename Peer>
    requires AgentPeer<Peer> || ClientPeer<Peer>
class Connection {
public:
    /// JSON codec of the peer.
    using Codec = typename Peer::codec_type;

    /// @param peer Agent or client; must outlive the connection.
    /// @param channel Transport.
    /// @param options Dispatch policy and error reporting.
    Connection(Peer& peer, std::shared_ptr<LineChannel> channel, ConnectionOptions options = {})
        : peer_(peer),
          channel_(std::move(channel)),
          writer_(std::make_shared<detail::Writer>(channel_)),
          options_(std::move(options)) {}

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /// Closes the connection and joins its threads.
    ~Connection() { close(); }

    /// Starts the background reader and dispatcher threads (no-op if already started).
    void start() {
        if (started_.exchange(true)) return;
        dispatcher_ = std::thread([this] { dispatch_loop(); });
        reader_ = std::thread([this] { read_loop(); });
    }

    /// Runs the reader on the calling thread until the peer closes its output, then shuts down.
    /// @return 0.
    int run() {
        if (started_.exchange(true)) {
            wait();
            return 0;
        }
        dispatcher_ = std::thread([this] { dispatch_loop(); });
        read_loop();
        return 0;
    }

    /// Blocks until the peer closes its output and all in-flight handlers finished.
    void wait() {
        {
            std::unique_lock lock(state_mutex_);
            state_cv_.wait(lock, [this] { return finished_ || !started_; });
        }
        std::lock_guard lock(join_mutex_);
        if (reader_.joinable()) reader_.join();
    }

    /// Waits up to @p timeout for the peer to close its output.
    /// @return `true` if the connection ended.
    template <typename Rep, typename Period>
    bool wait_for(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(state_mutex_);
        return state_cv_.wait_for(lock, timeout, [this] { return finished_ || !started_; });
    }

    /// Ends the connection: closes our output, waits for the peer to close its side (aborting the channel
    /// after ConnectionOptions::close_grace), fails pending requests and joins all threads. Idempotent.
    void close() {
        if (!started_) return;
        writer_->close();
        if (!wait_for(options_.close_grace)) {
            channel_->abort();
        }
        wait();
    }

    /// Reports whether the peer has ended the stream.
    [[nodiscard]] bool finished() const {
        std::lock_guard lock(state_mutex_);
        return finished_;
    }

    /// Thread-safe outbound sink bound to this connection.
    [[nodiscard]] ConnectionSink sink() const { return ConnectionSink{writer_}; }

    /// The peer this connection drives.
    Peer& peer() noexcept { return peer_; }

    /// The transport (e.g. `static_cast<ProcessChannel&>(conn.channel()).process()`).
    LineChannel& channel() noexcept { return *channel_; }

    // --- Client -> agent convenience API: each returns std::future<expected<Result, RpcError>> ---

    /// Sends `initialize`.
    auto initialize(const v1::InitializeParams& params = {}) requires ClientPeer<Peer> {
        return guard(peer_.send_initialize(params, sink(), use_future));
    }
    /// Sends `authenticate`.
    auto authenticate(const v1::AuthenticateParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_authenticate(params, sink(), use_future));
    }
    /// Sends `logout`.
    auto logout(const v1::LogoutParams& params = {}) requires ClientPeer<Peer> {
        return guard(peer_.send_logout(params, sink(), use_future));
    }
    /// Sends `session/new`.
    auto new_session(const v1::SessionNewParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_new(params, sink(), use_future));
    }
    /// Sends `session/load` (replayed history arrives through `on_session_update`).
    auto load_session(const v1::SessionLoadParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_load(params, sink(), use_future));
    }
    /// Sends `session/resume`.
    auto resume_session(const v1::SessionResumeParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_resume(params, sink(), use_future));
    }
    /// Sends `session/list`.
    auto list_sessions(const v1::SessionListParams& params = {}) requires ClientPeer<Peer> {
        return guard(peer_.send_session_list(params, sink(), use_future));
    }
    /// Sends `session/delete`.
    auto delete_session(const v1::SessionDeleteParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_delete(params, sink(), use_future));
    }
    /// Sends `session/close`.
    auto close_session(const v1::SessionCloseParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_close(params, sink(), use_future));
    }
    /// Sends `session/set_mode`.
    auto set_mode(const v1::SessionSetModeParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_set_mode(params, sink(), use_future));
    }
    /// Sends `session/set_config_option`.
    auto set_config_option(const v1::SessionSetConfigOptionParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_set_config_option(params, sink(), use_future));
    }
    /// Sends `session/set_model`.
    auto set_model(const v1::SessionSetModelParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_set_model(params, sink(), use_future));
    }
    /// Sends `session/prompt`; streamed updates reach `on_session_update` before the reply.
    auto prompt(const v1::SessionPromptParams& params) requires ClientPeer<Peer> {
        return guard(peer_.send_session_prompt(params, sink(), use_future));
    }
    /// Sends the `session/cancel` notification.
    void cancel(const v1::SessionCancelParams& params) requires ClientPeer<Peer> {
        peer_.send_session_cancel(params, sink());
    }

    // --- Agent -> client convenience API: each returns std::future<expected<Result, RpcError>> ---

    /// Sends a `session/update` notification.
    void session_update(const v1::SessionUpdateNotification& update) requires AgentPeer<Peer> {
        peer_.emit_session_update(update, sink());
    }
    /// Sends `session/request_permission`.
    auto request_permission(const v1::RequestPermissionParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_permission(params, sink(), use_future));
    }
    /// Sends `fs/read_text_file`.
    auto read_text_file(const v1::FsReadTextFileParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_fs_read(params, sink(), use_future));
    }
    /// Sends `fs/write_text_file`.
    auto write_text_file(const v1::FsWriteTextFileParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_fs_write(params, sink(), use_future));
    }
    /// Sends `terminal/create`.
    auto create_terminal(const v1::TerminalCreateParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_terminal_create(params, sink(), use_future));
    }
    /// Sends `terminal/output`.
    auto terminal_output(const v1::TerminalOutputParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_terminal_output(params, sink(), use_future));
    }
    /// Sends `terminal/wait_for_exit`.
    auto wait_for_terminal_exit(const v1::TerminalWaitForExitParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_terminal_wait_for_exit(params, sink(), use_future));
    }
    /// Sends `terminal/kill`.
    auto kill_terminal(const v1::TerminalKillParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_terminal_kill(params, sink(), use_future));
    }
    /// Sends `terminal/release`.
    auto release_terminal(const v1::TerminalReleaseParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_terminal_release(params, sink(), use_future));
    }
    /// Sends `elicitation/create`.
    auto elicit(const v1::CreateElicitationParams& params) requires AgentPeer<Peer> {
        return guard(peer_.request_elicitation(params, sink(), use_future));
    }

    // --- Both roles ---

    /// Sends any request and decodes the reply as @p Result (e.g. methods this SDK does not model yet).
    /// @param method JSON-RPC method name.
    /// @param params Parameters (any type the codec can encode).
    /// @return Future of the decoded reply.
    template <typename Result, typename Params>
    std::future<expected<Result, RpcError>> request(std::string_view method, const Params& params) {
        return guard(peer_.template send_request<Result>(method, params, sink(), use_future));
    }
    /// Sends a custom `_`-prefixed extension notification.
    /// @param method Extension method name.
    /// @param params Raw JSON parameters.
    void notify(std::string_view method, const RawJson& params) { peer_.send_ext_notification(method, params, sink()); }

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    Peer& peer_;
    std::shared_ptr<LineChannel> channel_;
    std::shared_ptr<detail::Writer> writer_;
    ConnectionOptions options_;
    std::atomic<bool> started_{false};
    std::thread reader_{};
    std::thread dispatcher_{};
    std::mutex join_mutex_{};

    std::mutex queue_mutex_{};
    std::condition_variable queue_cv_{};
    std::deque<std::string> queue_{};
    bool eof_{false};
    std::atomic<bool> dispatcher_done_{false};

    std::mutex workers_mutex_{};
    std::vector<Worker> workers_{};

    mutable std::mutex state_mutex_{};
    std::condition_variable state_cv_{};
    bool finished_{false};

    // A request sent after the writer closed was dropped: fail it (and any other reply that cannot arrive).
    template <typename Future>
    Future guard(Future future) {
        if (!writer_->is_open()) peer_.cancel_all_pending(RpcError::internal_error("Connection closed"));
        return future;
    }

    void report(std::string_view message) const {
        if (options_.on_error) {
            options_.on_error(message);
        } else {
            std::cerr << "[buraq] " << message << '\n';
        }
    }

    void feed(const std::string& frame) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        try {
#endif
            auto status = peer_.feed(std::as_bytes(std::span{frame.data(), frame.size()}), sink());
            if (!status) report("frame error: " + status.error().message);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
        } catch (const std::exception& ex) {
            report(std::string("handler threw: ") + ex.what());
        } catch (...) {
            report("handler threw a non-standard exception");
        }
#endif
    }

    // Requests go to the dispatcher; responses, notifications and malformed frames are handled inline.
    static bool is_request(const std::string& frame) {
        auto env = Codec::parse_envelope(std::as_bytes(std::span{frame.data(), frame.size()}));
        return env && !env->method.empty() && env->id.has_value();
    }

    void read_loop() {
        std::string line;
        while (channel_->read_line(line)) {
            if (!is_request(line)) {
                feed(line);
                continue;
            }
            std::lock_guard lock(queue_mutex_);
            queue_.push_back(std::move(line));
            queue_cv_.notify_one();
        }
        shutdown();
    }

    void dispatch_loop() {
        for (;;) {
            std::string frame;
            {
                std::unique_lock lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return eof_ || !queue_.empty(); });
                if (queue_.empty()) break;
                frame = std::move(queue_.front());
                queue_.pop_front();
            }
            if (options_.dispatch == Dispatch::concurrent) {
                spawn_worker(std::move(frame));
            } else {
                feed(frame);
            }
        }
        dispatcher_done_ = true;
    }

    void spawn_worker(std::string frame) {
        std::lock_guard lock(workers_mutex_);
        std::erase_if(workers_, [](Worker& w) {
            if (!w.done->load()) return false;
            w.thread.join();
            return true;
        });
        auto done = std::make_shared<std::atomic<bool>>(false);
        workers_.push_back(Worker{std::thread([this, done, f = std::move(frame)] {
                                      feed(f);
                                      done->store(true);
                                  }),
                                  done});
    }

    bool workers_busy() {
        std::lock_guard lock(workers_mutex_);
        for (const auto& w : workers_) {
            if (!w.done->load()) return true;
        }
        return false;
    }

    // Peer closed its output: drain queued work, failing replies that can no longer arrive so handlers
    // blocked on them return.
    void shutdown() {
        {
            std::lock_guard lock(queue_mutex_);
            eof_ = true;
            queue_cv_.notify_all();
        }
        const auto closed = RpcError::internal_error("Connection closed");
        while (!dispatcher_done_ || workers_busy()) {
            peer_.cancel_all_pending(closed);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (dispatcher_.joinable()) dispatcher_.join();
        {
            std::lock_guard lock(workers_mutex_);
            for (auto& w : workers_) w.thread.join();
            workers_.clear();
        }
        peer_.cancel_all_pending(closed);
        writer_->close();
        std::lock_guard lock(state_mutex_);
        finished_ = true;
        state_cv_.notify_all();
    }
};

/// Serves @p agent over stdin/stdout until the client closes stdin.
///
/// Threaded counterpart of transport::run_stdio_agent(): hooks may block on `use_future` replies from the
/// client and deferred Responders may reply from any thread.
/// @tparam Agent AgentBase-derived class.
/// @param agent The agent.
/// @param options Dispatch policy and error reporting.
/// @return 0.
template <AgentPeer Agent>
int serve_stdio(Agent& agent, ConnectionOptions options = {}) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    Connection<Agent> conn(agent, std::make_shared<StreamChannel>(std::cin, std::cout), std::move(options));
    return conn.run();
}

/// Launches an agent subprocess and connects @p client to its stdio.
///
/// The connection is already running: call `initialize()` next. Destroying it closes the agent's stdin and
/// terminates the agent if it does not exit within ConnectionOptions::close_grace.
/// @tparam Client ClientBase-derived class.
/// @param client The client; must outlive the connection.
/// @param argv Agent command line (argv[0] is looked up in `PATH`).
/// @param spawn Environment, working directory and stderr handling for the agent.
/// @param options Dispatch policy and error reporting.
/// @return The running connection, or a human-readable error if the agent could not be started.
template <ClientPeer Client>
expected<std::unique_ptr<Connection<Client>>, std::string> spawn_agent(Client& client,
                                                                       const std::vector<std::string>& argv,
                                                                       const transport::SpawnOptions& spawn = {},
                                                                       ConnectionOptions options = {}) {
    auto process = transport::Process::spawn(argv, spawn);
    if (!process) return unexpected(std::move(process.error()));
    auto conn = std::make_unique<Connection<Client>>(
        client, std::make_shared<ProcessChannel>(std::move(*process)), std::move(options));
    conn->start();
    return conn;
}

} // namespace buraq::runtime
