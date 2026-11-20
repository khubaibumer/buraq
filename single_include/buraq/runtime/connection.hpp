// ============================================================================
// buraq v0.1.0 - Agent Client Protocol (ACP) v1 SDK for C++20/23
// https://github.com/khubaibumer/buraq
//
// Single-header distribution: buraq/runtime/connection.hpp
// Requires buraq/acp.hpp from this single_include tree; link with threads.
//
// GENERATED FILE - DO NOT EDIT. Regenerate with: python3 scripts/amalgamate.py
// SPDX-License-Identifier: Apache-2.0
// ============================================================================
#pragma once

#include "../acp.hpp"

// ---- begin buraq/transport/process.hpp ----
/// @file
/// @brief Portable child process with piped stdio (`buraq::transport::Process`).
///
/// Opt-in header (not included by buraq/acp.hpp). Used by buraq/runtime/connection.hpp to launch ACP
/// agents (`spawn_agent`), but usable on its own: spawn a command, write NDJSON frames to its stdin, read
/// newline-delimited frames from its stdout. POSIX (fork/exec) and Windows (CreateProcess) are supported.

#include <chrono>
#include <cstddef>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
/// @cond INTERNAL
extern "C" char **environ; // NOLINT: POSIX global, not declared by every libc header
/// @endcond
#endif

namespace buraq::transport {
    /// Options for Process::spawn().
    struct SpawnOptions {
        /// Environment variables to add or override in the child.
        std::map<std::string, std::string> env{};
        /// Start from the parent's environment (`true`) or from an empty one (`false`).
        bool inherit_env{true};
        /// Working directory of the child; the parent's when empty.
        std::optional<std::string> cwd{};
        /// Forward the child's stderr to the parent's (`true`) or discard it (`false`). Agents log to stderr.
        bool inherit_stderr{true};
    };

    /// A child process whose stdin/stdout are pipes owned by this object.
    ///
    /// Move-only. read_line() and write() may be used concurrently from two threads (one reader, one writer),
    /// but neither is safe to call concurrently with itself. The destructor closes stdin, gives the child a
    /// short grace period to exit, then terminates and reaps it.
    ///
    /// On POSIX, spawning sets `SIGPIPE` to `SIG_IGN` when it is still at its default disposition, so writing
    /// to an exited child fails with an error instead of killing the parent process.
    class Process {
    public:
        /// Launches @p argv (argv[0] is looked up in `PATH`) with piped stdin/stdout.
        /// @param argv Program and arguments; must not be empty.
        /// @param options Environment, working directory and stderr handling.
        /// @return The running process, or a human-readable error (e.g. the program was not found).
        static expected<Process, std::string> spawn(const std::vector<std::string> &argv,
                                                    const SpawnOptions &options = {}) {
            if (argv.empty() || argv.front().empty()) {
                return unexpected(std::string("spawn: empty command line"));
            }
            Process p;
            if (auto err = p.start(argv, options)) {
                return unexpected(std::move(*err));
            }
            return p;
        }

        Process() = default;

        Process(const Process &) = delete;

        Process &operator=(const Process &) = delete;

        /// Transfers ownership of the child and its pipes.
        Process(Process &&other) noexcept { swap(other); }
        /// Terminates any currently owned child, then takes over @p other's.
        Process &operator=(Process &&other) noexcept {
            if (this != &other) {
                Process tmp(std::move(other));
                swap(tmp);
            }
            return *this;
        }

        /// Closes stdin, waits up to one second for a voluntary exit, then terminates and reaps the child.
        ~Process() {
            if (!running_) {
                close_handles();
                return;
            }
            close_stdin();
            terminate(std::chrono::milliseconds(1000));
            close_handles();
        }

        /// Reports whether this object owns a child that has not been reaped yet.
        [[nodiscard]] bool running() const noexcept { return running_; }

#if defined(_WIN32)
        /// Process id of the child.
        [[nodiscard]] unsigned long pid() const noexcept { return pid_; }
#else
        /// Process id of the child.
        [[nodiscard]] pid_t pid() const noexcept { return pid_; }
#endif

        /// Reads one line from the child's stdout (blocking).
        /// @param line Receives the line without its trailing `\n` / `\r\n`.
        /// @return `false` at end of stream (the child closed stdout or exited) with no more buffered lines.
        bool read_line(std::string &line) {
            for (;;) {
                if (const auto nl = buffer_.find('\n', scan_from_); nl != std::string::npos) {
                    line.assign(buffer_, 0, nl);
                    buffer_.erase(0, nl + 1);
                    scan_from_ = 0;
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }
                scan_from_ = buffer_.size();
                char chunk[8192];
                const auto n = read_some(chunk, sizeof(chunk));
                if (n <= 0) {
                    if (buffer_.empty()) return false;
                    line = std::move(buffer_); // final unterminated line
                    buffer_.clear();
                    scan_from_ = 0;
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    return true;
                }
                buffer_.append(chunk, static_cast<std::size_t>(n));
            }
        }

        /// Writes all of @p data to the child's stdin (blocking).
        /// @return `false` if stdin is closed or the write failed (e.g. the child exited).
        bool write(std::string_view data) {
            while (!data.empty()) {
                const auto n = write_some(data.data(), data.size());
                if (n <= 0) return false;
                data.remove_prefix(static_cast<std::size_t>(n));
            }
            return true;
        }

        /// Closes the child's stdin, signalling end of input (agents exit on stdin EOF).
        void close_stdin() noexcept {
#if defined(_WIN32)
            if (stdin_ != nullptr) {
                CloseHandle(stdin_);
                stdin_ = nullptr;
            }
#else
            if (stdin_ >= 0) {
                ::close(stdin_);
                stdin_ = -1;
            }
#endif
        }

        /// Returns the exit code if the child has exited, without blocking.
        /// @return The exit code (128 + signal number for a signal on POSIX), or `std::nullopt` while running.
        std::optional<int> try_wait() {
            if (!running_) return exit_code_;
#if defined(_WIN32)
            if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return std::nullopt;
            reap_windows();
#else
            int status = 0;
            const pid_t r = ::waitpid(pid_, &status, WNOHANG);
            if (r == 0) return std::nullopt;
            record_status(r == pid_ ? status : 0, r == pid_);
#endif
            return exit_code_;
        }

        /// Blocks until the child exits.
        /// @return The exit code (128 + signal number for a signal on POSIX).
        int wait() {
            if (!running_) return exit_code_.value_or(-1);
#if defined(_WIN32)
            WaitForSingleObject(process_, INFINITE);
            reap_windows();
#else
            int status = 0;
            pid_t r = -1;
            do {
                r = ::waitpid(pid_, &status, 0);
            } while (r < 0 && errno == EINTR);
            record_status(r == pid_ ? status : 0, r == pid_);
#endif
            return exit_code_.value_or(-1);
        }

        /// Stops the child: waits up to @p grace for it to exit (after a `SIGTERM` on POSIX), then kills it.
        /// @param grace Time allowed for a graceful exit.
        /// @return The exit code.
        int terminate(std::chrono::milliseconds grace = std::chrono::milliseconds(2000)) {
            if (!running_) return exit_code_.value_or(-1);
#if !defined(_WIN32)
            if (!try_wait()) ::kill(pid_, SIGTERM);
#endif
            const auto deadline = std::chrono::steady_clock::now() + grace;
            while (running_ && !try_wait() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (running_) {
#if defined(_WIN32)
                TerminateProcess(process_, 1);
#else
                ::kill(pid_, SIGKILL);
#endif
            }
            return wait();
        }

    private:
#if defined(_WIN32)
        HANDLE process_{nullptr};
        HANDLE stdin_{nullptr};
        HANDLE stdout_{nullptr};
        unsigned long pid_{0};
#else
        pid_t pid_{-1};
        int stdin_{-1};
        int stdout_{-1};
#endif
        bool running_{false};
        std::optional<int> exit_code_{};
        std::string buffer_{};
        std::size_t scan_from_{0};

        void swap(Process &o) noexcept {
#if defined(_WIN32)
            std::swap(process_, o.process_);
#endif
            std::swap(pid_, o.pid_);
            std::swap(stdin_, o.stdin_);
            std::swap(stdout_, o.stdout_);
            std::swap(running_, o.running_);
            std::swap(exit_code_, o.exit_code_);
            std::swap(buffer_, o.buffer_);
            std::swap(scan_from_, o.scan_from_);
        }

#if defined(_WIN32)
        void close_handles() noexcept {
            close_stdin();
            if (stdout_ != nullptr) {
                CloseHandle(stdout_);
                stdout_ = nullptr;
            }
            if (process_ != nullptr) {
                CloseHandle(process_);
                process_ = nullptr;
            }
        }

        void reap_windows() {
            DWORD code = 0;
            GetExitCodeProcess(process_, &code);
            exit_code_ = static_cast<int>(code);
            running_ = false;
        }

        long read_some(char *buf, std::size_t size) {
            if (stdout_ == nullptr) return -1;
            DWORD n = 0;
            if (!ReadFile(stdout_, buf, static_cast<DWORD>(size), &n, nullptr)) return -1;
            return static_cast<long>(n);
        }

        long write_some(const char *buf, std::size_t size) {
            if (stdin_ == nullptr) return -1;
            DWORD n = 0;
            if (!WriteFile(stdin_, buf, static_cast<DWORD>(size), &n, nullptr)) return -1;
            return static_cast<long>(n);
        }

        static std::string quote_arg(const std::string &arg) {
            if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
            std::string out = "\"";
            for (auto it = arg.begin();; ++it) {
                std::size_t backslashes = 0;
                while (it != arg.end() && *it == '\\') {
                    ++it;
                    ++backslashes;
                }
                if (it == arg.end()) {
                    out.append(backslashes * 2, '\\');
                    break;
                }
                if (*it == '"') {
                    out.append(backslashes * 2 + 1, '\\');
                } else {
                    out.append(backslashes, '\\');
                }
                out.push_back(*it);
            }
            out.push_back('"');
            return out;
        }

        struct CaseInsensitiveLess {
            bool operator()(const std::string &a, const std::string &b) const {
                return _stricmp(a.c_str(), b.c_str()) < 0;
            }
        };

        static std::string environment_block(const SpawnOptions &options) {
            std::map<std::string, std::string, CaseInsensitiveLess> vars;
            if (options.inherit_env) {
                if (char *block = GetEnvironmentStringsA()) {
                    for (const char *p = block; *p != '\0'; p += std::strlen(p) + 1) {
                        std::string_view entry{p};
                        const auto eq = entry.find('=', 1); // entries like "=C:=C:\\" start with '='
                        if (eq == std::string_view::npos) continue;
                        vars[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
                    }
                    FreeEnvironmentStringsA(block);
                }
            }
            for (const auto &[k, v]: options.env) vars[k] = v;
            std::string out;
            for (const auto &[k, v]: vars) {
                out += k;
                out += '=';
                out += v;
                out.push_back('\0');
            }
            out.push_back('\0');
            return out;
        }

        static std::string last_error_message(const char *what) {
            return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
        }

        std::optional<std::string> start(const std::vector<std::string> &argv, const SpawnOptions &options) {
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE child_in = nullptr;
            HANDLE child_out = nullptr;
            if (!CreatePipe(&child_in, &stdin_, &sa, 0)) return last_error_message("CreatePipe");
            if (!CreatePipe(&stdout_, &child_out, &sa, 0)) {
                CloseHandle(child_in);
                return last_error_message("CreatePipe");
            }
            SetHandleInformation(stdin_, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdout_, HANDLE_FLAG_INHERIT, 0);

            HANDLE child_err = nullptr;
            if (options.inherit_stderr) {
                child_err = GetStdHandle(STD_ERROR_HANDLE);
            } else {
                child_err = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
            }

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = child_in;
            si.hStdOutput = child_out;
            si.hStdError = child_err;

            std::string cmdline;
            for (const auto &arg: argv) {
                if (!cmdline.empty()) cmdline += ' ';
                cmdline += quote_arg(arg);
            }
            const bool custom_env = !options.inherit_env || !options.env.empty();
            std::string env_block = custom_env ? environment_block(options) : std::string{};

            PROCESS_INFORMATION pi{};
            const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0,
                                           custom_env ? env_block.data() : nullptr,
                                           options.cwd ? options.cwd->c_str() : nullptr, &si, &pi);
            const auto err = ok
                                 ? std::optional<std::string>{}
                                 : last_error_message(("CreateProcess(" + argv.front() + ")").c_str());
            CloseHandle(child_in);
            CloseHandle(child_out);
            if (!options.inherit_stderr && child_err != INVALID_HANDLE_VALUE && child_err != nullptr) {
                CloseHandle(child_err);
            }
            if (!ok) {
                close_handles();
                return err;
            }
            CloseHandle(pi.hThread);
            process_ = pi.hProcess;
            pid_ = pi.dwProcessId;
            running_ = true;
            return std::nullopt;
        }
#else
        void close_handles() noexcept {
            close_stdin();
            if (stdout_ >= 0) {
                ::close(stdout_);
                stdout_ = -1;
            }
        }

        void record_status(int status, bool reaped) {
            running_ = false;
            if (!reaped) {
                exit_code_ = -1;
            } else if (WIFEXITED(status)) {
                exit_code_ = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                exit_code_ = 128 + WTERMSIG(status);
            } else {
                exit_code_ = -1;
            }
        }

        long read_some(char *buf, std::size_t size) {
            if (stdout_ < 0) return -1;
            for (;;) {
                const auto n = ::read(stdout_, buf, size);
                if (n < 0 && errno == EINTR) continue;
                return static_cast<long>(n);
            }
        }

        long write_some(const char *buf, std::size_t size) {
            if (stdin_ < 0) return -1;
            for (;;) {
                const auto n = ::write(stdin_, buf, size);
                if (n < 0 && errno == EINTR) continue;
                return static_cast<long>(n);
            }
        }

        static bool make_pipe(int fds[2]) {
            if (::pipe(fds) != 0) return false;
            ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
            return true;
        }

        std::optional<std::string> start(const std::vector<std::string> &argv, const SpawnOptions &options) {
            struct sigaction current{};
            if (::sigaction(SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL) {
                std::signal(SIGPIPE, SIG_IGN);
            }

            // Everything the child needs is prepared before fork(): only async-signal-safe calls follow it.
            std::vector<char *> args;
            args.reserve(argv.size() + 1);
            for (const auto &a: argv) args.push_back(const_cast<char *>(a.c_str()));
            args.push_back(nullptr);

            const bool custom_env = !options.inherit_env || !options.env.empty();
            std::vector<std::string> env_storage;
            std::vector<char *> envp;
            if (custom_env) {
                std::map<std::string, std::string> vars;
                if (options.inherit_env) {
                    for (char **e = environ; e != nullptr && *e != nullptr; ++e) {
                        std::string_view entry{*e};
                        const auto eq = entry.find('=');
                        if (eq == std::string_view::npos) continue;
                        vars[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
                    }
                }
                for (const auto &[k, v]: options.env) vars[k] = v;
                env_storage.reserve(vars.size());
                for (const auto &[k, v]: vars) env_storage.push_back(k + "=" + v);
                for (auto &s: env_storage) envp.push_back(s.data());
                envp.push_back(nullptr);
            }

            int in_pipe[2];
            int out_pipe[2];
            int err_pipe[2]; // reports exec failure (errno) to the parent; closed on successful exec
            if (!make_pipe(in_pipe)) return std::string("pipe failed: ") + std::strerror(errno);
            if (!make_pipe(out_pipe)) {
                const int e = errno;
                ::close(in_pipe[0]);
                ::close(in_pipe[1]);
                return std::string("pipe failed: ") + std::strerror(e);
            }
            if (!make_pipe(err_pipe)) {
                const int e = errno;
                for (int fd: {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) ::close(fd);
                return std::string("pipe failed: ") + std::strerror(e);
            }

            const pid_t pid = ::fork();
            if (pid < 0) {
                const int e = errno;
                for (int fd: {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) ::close(fd);
                return std::string("fork failed: ") + std::strerror(e);
            }
            if (pid == 0) {
                int stage_errno = 0;
                if (::dup2(in_pipe[0], STDIN_FILENO) < 0 || ::dup2(out_pipe[1], STDOUT_FILENO) < 0) {
                    stage_errno = errno;
                }
                if (stage_errno == 0 && !options.inherit_stderr) {
                    const int devnull = ::open("/dev/null", O_WRONLY);
                    if (devnull >= 0) {
                        ::dup2(devnull, STDERR_FILENO);
                        ::close(devnull);
                    }
                }
                if (stage_errno == 0 && options.cwd && ::chdir(options.cwd->c_str()) != 0) {
                    stage_errno = errno;
                }
                if (stage_errno == 0) {
                    if (custom_env) environ = envp.data();
                    ::execvp(args[0], args.data());
                    stage_errno = errno;
                }
                [[maybe_unused]] const auto w = ::write(err_pipe[1], &stage_errno, sizeof(stage_errno));
                ::_exit(127);
            }

            ::close(in_pipe[0]);
            ::close(out_pipe[1]);
            ::close(err_pipe[1]);
            stdin_ = in_pipe[1];
            stdout_ = out_pipe[0];
            pid_ = pid;
            running_ = true;

            int child_errno = 0;
            long n = 0;
            do {
                n = ::read(err_pipe[0], &child_errno, sizeof(child_errno));
            } while (n < 0 && errno == EINTR);
            ::close(err_pipe[0]);
            if (n == static_cast<long>(sizeof(child_errno))) {
                wait();
                close_handles();
                return "cannot start '" + argv.front() + "': " + std::strerror(child_errno);
            }
            return std::nullopt;
        }
#endif
    };
} // namespace buraq::transport
// ---- end buraq/transport/process.hpp ----

// ---- begin buraq/runtime/connection.hpp ----
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

namespace buraq::runtime {
    /// A bidirectional, newline-delimited byte channel (one JSON-RPC message per line).
    class LineChannel {
    public:
        virtual ~LineChannel() = default;

        /// Reads one non-empty line (blocking), without its terminator.
        /// @param line Receives the line.
        /// @return `false` at end of stream.
        virtual bool read_line(std::string &line) = 0;

        /// Writes @p frame followed by a newline and flushes.
        /// @param frame One serialized message.
        /// @return `false` if the channel is closed or broken.
        virtual bool write_line(std::string_view frame) = 0;

        /// Signals end of output to the peer (e.g. closes the child's stdin). Must be safe to call repeatedly.
        virtual void close_output() {
        }

        /// Forcibly unblocks a pending read_line() (e.g. by terminating the child). Called by Connection::close()
        /// when the peer does not end the stream after close_output().
        virtual void abort() {
        }
    };

    /// LineChannel over a pair of standard streams (e.g. std::cin / std::cout).
    class StreamChannel final : public LineChannel {
    public:
        /// @param in Input stream.
        /// @param out Output stream.
        StreamChannel(std::istream &in, std::ostream &out) : in_(in), out_(out) {
        }

        bool read_line(std::string &line) override {
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
        std::istream &in_;
        std::ostream &out_;
    };

    /// LineChannel over a child process's stdin/stdout.
    class ProcessChannel final : public LineChannel {
    public:
        /// @param process A running process (ownership is taken).
        explicit ProcessChannel(transport::Process process) : process_(std::move(process)) {
        }

        bool read_line(std::string &line) override {
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
        transport::Process &process() noexcept { return process_; }

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
            explicit Writer(std::shared_ptr<LineChannel> channel) : channel_(std::move(channel)) {
            }

            /// Writes one frame unless closed. @return `false` if the writer is (or just became) closed.
            bool write(std::span<const std::byte> bytes) {
                const std::string_view frame{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
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
        explicit ConnectionSink(std::shared_ptr<detail::Writer> writer) : writer_(std::move(writer)) {
        }

        /// Writes one frame (a newline is appended).
        /// @param bytes Serialized message.
        void operator()(std::span<const std::byte> bytes) const { writer_->write(bytes); }

    private:
        std::shared_ptr<detail::Writer> writer_;
    };

    /// Satisfied by buraq::AgentBase-derived classes.
    template<typename Peer>
    concept AgentPeer = requires { typename Peer::codec_type; } &&
                        std::derived_from<Peer, AgentBase<Peer, typename Peer::codec_type> >;

    /// Satisfied by buraq::ClientBase-derived classes.
    template<typename Peer>
    concept ClientPeer = requires { typename Peer::codec_type; } &&
                         std::derived_from<Peer, ClientBase<Peer, typename Peer::codec_type> >;

    /// Runs one ACP peer (agent or client) over a LineChannel with background threads.
    ///
    /// Not copyable or movable (threads refer to it); spawn_agent() returns it in a `std::unique_ptr`.
    /// The peer must outlive the connection. See the file documentation for the threading model.
    /// @tparam Peer An AgentBase- or ClientBase-derived class.
    template<typename Peer>
        requires AgentPeer<Peer> || ClientPeer<Peer>
    class Connection {
    public:
        /// JSON codec of the peer.
        using Codec = typename Peer::codec_type;

        /// @param peer Agent or client; must outlive the connection.
        /// @param channel Transport.
        /// @param options Dispatch policy and error reporting.
        Connection(Peer &peer, std::shared_ptr<LineChannel> channel, ConnectionOptions options = {})
            : peer_(peer),
              channel_(std::move(channel)),
              writer_(std::make_shared<detail::Writer>(channel_)),
              options_(std::move(options)) {
        }

        Connection(const Connection &) = delete;

        Connection &operator=(const Connection &) = delete;

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
        template<typename Rep, typename Period>
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
        Peer &peer() noexcept { return peer_; }

        /// The transport (e.g. `static_cast<ProcessChannel&>(conn.channel()).process()`).
        LineChannel &channel() noexcept { return *channel_; }

        // --- Client -> agent convenience API: each returns std::future<expected<Result, RpcError>> ---

        /// Sends `initialize`.
        auto initialize(const v1::InitializeParams &params = {}) requires ClientPeer<Peer> {
            return guard(peer_.send_initialize(params, sink(), use_future));
        }

        /// Sends `authenticate`.
        auto authenticate(const v1::AuthenticateParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_authenticate(params, sink(), use_future));
        }

        /// Sends `logout`.
        auto logout(const v1::LogoutParams &params = {}) requires ClientPeer<Peer> {
            return guard(peer_.send_logout(params, sink(), use_future));
        }

        /// Sends `session/new`.
        auto new_session(const v1::SessionNewParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_new(params, sink(), use_future));
        }

        /// Sends `session/load` (replayed history arrives through `on_session_update`).
        auto load_session(const v1::SessionLoadParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_load(params, sink(), use_future));
        }

        /// Sends `session/resume`.
        auto resume_session(const v1::SessionResumeParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_resume(params, sink(), use_future));
        }

        /// Sends `session/list`.
        auto list_sessions(const v1::SessionListParams &params = {}) requires ClientPeer<Peer> {
            return guard(peer_.send_session_list(params, sink(), use_future));
        }

        /// Sends `session/delete`.
        auto delete_session(const v1::SessionDeleteParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_delete(params, sink(), use_future));
        }

        /// Sends `session/close`.
        auto close_session(const v1::SessionCloseParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_close(params, sink(), use_future));
        }

        /// Sends `session/set_mode`.
        auto set_mode(const v1::SessionSetModeParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_set_mode(params, sink(), use_future));
        }

        /// Sends `session/set_config_option`.
        auto set_config_option(const v1::SessionSetConfigOptionParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_set_config_option(params, sink(), use_future));
        }

        /// Sends `session/set_model`.
        auto set_model(const v1::SessionSetModelParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_set_model(params, sink(), use_future));
        }

        /// Sends `session/prompt`; streamed updates reach `on_session_update` before the reply.
        auto prompt(const v1::SessionPromptParams &params) requires ClientPeer<Peer> {
            return guard(peer_.send_session_prompt(params, sink(), use_future));
        }

        /// Sends the `session/cancel` notification.
        void cancel(const v1::SessionCancelParams &params) requires ClientPeer<Peer> {
            peer_.send_session_cancel(params, sink());
        }

        // --- Agent -> client convenience API: each returns std::future<expected<Result, RpcError>> ---

        /// Sends a `session/update` notification.
        void session_update(const v1::SessionUpdateNotification &update) requires AgentPeer<Peer> {
            peer_.emit_session_update(update, sink());
        }

        /// Sends `session/request_permission`.
        auto request_permission(const v1::RequestPermissionParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_permission(params, sink(), use_future));
        }

        /// Sends `fs/read_text_file`.
        auto read_text_file(const v1::FsReadTextFileParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_fs_read(params, sink(), use_future));
        }

        /// Sends `fs/write_text_file`.
        auto write_text_file(const v1::FsWriteTextFileParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_fs_write(params, sink(), use_future));
        }

        /// Sends `terminal/create`.
        auto create_terminal(const v1::TerminalCreateParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_terminal_create(params, sink(), use_future));
        }

        /// Sends `terminal/output`.
        auto terminal_output(const v1::TerminalOutputParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_terminal_output(params, sink(), use_future));
        }

        /// Sends `terminal/wait_for_exit`.
        auto wait_for_terminal_exit(const v1::TerminalWaitForExitParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_terminal_wait_for_exit(params, sink(), use_future));
        }

        /// Sends `terminal/kill`.
        auto kill_terminal(const v1::TerminalKillParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_terminal_kill(params, sink(), use_future));
        }

        /// Sends `terminal/release`.
        auto release_terminal(const v1::TerminalReleaseParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_terminal_release(params, sink(), use_future));
        }

        /// Sends `elicitation/create`.
        auto elicit(const v1::CreateElicitationParams &params) requires AgentPeer<Peer> {
            return guard(peer_.request_elicitation(params, sink(), use_future));
        }

        // --- Both roles ---

        /// Sends any request and decodes the reply as @p Result (e.g. methods this SDK does not model yet).
        /// @param method JSON-RPC method name.
        /// @param params Parameters (any type the codec can encode).
        /// @return Future of the decoded reply.
        template<typename Result, typename Params>
        std::future<expected<Result, RpcError> > request(std::string_view method, const Params &params) {
            return guard(peer_.template send_request<Result>(method, params, sink(), use_future));
        }

        /// Sends a custom `_`-prefixed extension notification.
        /// @param method Extension method name.
        /// @param params Raw JSON parameters.
        void notify(std::string_view method, const RawJson &params) {
            peer_.send_ext_notification(method, params, sink());
        }

    private:
        struct Worker {
            std::thread thread;
            std::shared_ptr<std::atomic<bool> > done;
        };

        Peer &peer_;
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
        template<typename Future>
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

        void feed(const std::string &frame) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try {
#endif
                auto status = peer_.feed(std::as_bytes(std::span{frame.data(), frame.size()}), sink());
                if (!status) report("frame error: " + status.error().message);
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            } catch (const std::exception &ex) {
                report(std::string("handler threw: ") + ex.what());
            } catch (...) {
                report("handler threw a non-standard exception");
            }
#endif
        }

        // Requests go to the dispatcher; responses, notifications and malformed frames are handled inline.
        static bool is_request(const std::string &frame) {
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
            std::erase_if(workers_, [](Worker &w) {
                if (!w.done->load()) return false;
                w.thread.join();
                return true;
            });
            auto done = std::make_shared<std::atomic<bool> >(false);
            workers_.push_back(Worker{
                std::thread([this, done, f = std::move(frame)] {
                    feed(f);
                    done->store(true);
                }),
                done
            });
        }

        bool workers_busy() {
            std::lock_guard lock(workers_mutex_);
            for (const auto &w: workers_) {
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
                for (auto &w: workers_) w.thread.join();
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
    template<AgentPeer Agent>
    int serve_stdio(Agent &agent, ConnectionOptions options = {}) {
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
    template<ClientPeer Client>
    expected<std::unique_ptr<Connection<Client> >, std::string> spawn_agent(Client &client,
                                                                            const std::vector<std::string> &argv,
                                                                            const transport::SpawnOptions &spawn = {},
                                                                            ConnectionOptions options = {}) {
        auto process = transport::Process::spawn(argv, spawn);
        if (!process) return unexpected(std::move(process.error()));
        auto conn = std::make_unique<Connection<Client> >(
            client, std::make_shared<ProcessChannel>(std::move(*process)), std::move(options));
        conn->start();
        return conn;
    }
} // namespace buraq::runtime
// ---- end buraq/runtime/connection.hpp ----
