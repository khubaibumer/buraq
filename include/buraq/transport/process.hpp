/// @file
/// @brief Portable child process with piped stdio (`buraq::transport::Process`).
///
/// Opt-in header (not included by buraq/acp.hpp). Used by buraq/runtime/connection.hpp to launch ACP
/// agents (`spawn_agent`), but usable on its own: spawn a command, write NDJSON frames to its stdin, read
/// newline-delimited frames from its stdout. POSIX (fork/exec) and Windows (CreateProcess) are supported.
#pragma once

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
#include "buraq/detail/compat.hpp"

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
extern "C" char** environ; // NOLINT: POSIX global, not declared by every libc header
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
    static expected<Process, std::string> spawn(const std::vector<std::string>& argv, const SpawnOptions& options = {}) {
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
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    /// Transfers ownership of the child and its pipes.
    Process(Process&& other) noexcept { swap(other); }
    /// Terminates any currently owned child, then takes over @p other's.
    Process& operator=(Process&& other) noexcept {
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
    bool read_line(std::string& line) {
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

    void swap(Process& o) noexcept {
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

    long read_some(char* buf, std::size_t size) {
        if (stdout_ == nullptr) return -1;
        DWORD n = 0;
        if (!ReadFile(stdout_, buf, static_cast<DWORD>(size), &n, nullptr)) return -1;
        return static_cast<long>(n);
    }

    long write_some(const char* buf, std::size_t size) {
        if (stdin_ == nullptr) return -1;
        DWORD n = 0;
        if (!WriteFile(stdin_, buf, static_cast<DWORD>(size), &n, nullptr)) return -1;
        return static_cast<long>(n);
    }

    static std::string quote_arg(const std::string& arg) {
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
        bool operator()(const std::string& a, const std::string& b) const {
            return _stricmp(a.c_str(), b.c_str()) < 0;
        }
    };

    static std::string environment_block(const SpawnOptions& options) {
        std::map<std::string, std::string, CaseInsensitiveLess> vars;
        if (options.inherit_env) {
            if (char* block = GetEnvironmentStringsA()) {
                for (const char* p = block; *p != '\0'; p += std::strlen(p) + 1) {
                    std::string_view entry{p};
                    const auto eq = entry.find('=', 1); // entries like "=C:=C:\\" start with '='
                    if (eq == std::string_view::npos) continue;
                    vars[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
                }
                FreeEnvironmentStringsA(block);
            }
        }
        for (const auto& [k, v] : options.env) vars[k] = v;
        std::string out;
        for (const auto& [k, v] : vars) {
            out += k;
            out += '=';
            out += v;
            out.push_back('\0');
        }
        out.push_back('\0');
        return out;
    }

    static std::string last_error_message(const char* what) {
        return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
    }

    std::optional<std::string> start(const std::vector<std::string>& argv, const SpawnOptions& options) {
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
        for (const auto& arg : argv) {
            if (!cmdline.empty()) cmdline += ' ';
            cmdline += quote_arg(arg);
        }
        const bool custom_env = !options.inherit_env || !options.env.empty();
        std::string env_block = custom_env ? environment_block(options) : std::string{};

        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0,
                                       custom_env ? env_block.data() : nullptr,
                                       options.cwd ? options.cwd->c_str() : nullptr, &si, &pi);
        const auto err = ok ? std::optional<std::string>{} : last_error_message(("CreateProcess(" + argv.front() + ")").c_str());
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

    long read_some(char* buf, std::size_t size) {
        if (stdout_ < 0) return -1;
        for (;;) {
            const auto n = ::read(stdout_, buf, size);
            if (n < 0 && errno == EINTR) continue;
            return static_cast<long>(n);
        }
    }

    long write_some(const char* buf, std::size_t size) {
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

    std::optional<std::string> start(const std::vector<std::string>& argv, const SpawnOptions& options) {
        struct sigaction current{};
        if (::sigaction(SIGPIPE, nullptr, &current) == 0 && current.sa_handler == SIG_DFL) {
            std::signal(SIGPIPE, SIG_IGN);
        }

        // Everything the child needs is prepared before fork(): only async-signal-safe calls follow it.
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);

        const bool custom_env = !options.inherit_env || !options.env.empty();
        std::vector<std::string> env_storage;
        std::vector<char*> envp;
        if (custom_env) {
            std::map<std::string, std::string> vars;
            if (options.inherit_env) {
                for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
                    std::string_view entry{*e};
                    const auto eq = entry.find('=');
                    if (eq == std::string_view::npos) continue;
                    vars[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
                }
            }
            for (const auto& [k, v] : options.env) vars[k] = v;
            env_storage.reserve(vars.size());
            for (const auto& [k, v] : vars) env_storage.push_back(k + "=" + v);
            for (auto& s : env_storage) envp.push_back(s.data());
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
            for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) ::close(fd);
            return std::string("pipe failed: ") + std::strerror(e);
        }

        const pid_t pid = ::fork();
        if (pid < 0) {
            const int e = errno;
            for (int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1]}) ::close(fd);
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
