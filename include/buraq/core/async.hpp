#pragma once
/// @file
/// @brief Asynchronous request plumbing shared by AgentBase and ClientBase.
///
/// - buraq::Responder: completes an inbound request later (deferred replies), from any thread.
/// - buraq::use_future: completion token that turns any typed outbound request into a `std::future`.
/// - buraq::detail::PendingRequests: thread-safe id allocation and response correlation.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"

namespace buraq {
    /// Type-erased outbound sink: receives one serialized JSON-RPC message per call.
    using OutboundFn = std::function<void(std::span<const std::byte>)>;

    /// Completion token type; see buraq::use_future.
    struct use_future_t {
        explicit constexpr use_future_t() = default;
    };

    /// Completion token: pass it instead of a callback to receive a
    /// `std::future<expected<Result, RpcError>>` from any typed outbound request.
    ///
    /// @code
    /// auto session = client.send_session_new({.cwd = "/repo"}, conn.sink(), buraq::use_future).get();
    /// @endcode
    /// @warning Only block on the future from a thread other than the one feeding inbound frames
    ///          (buraq::Connection does this for you), otherwise the reply can never be processed.
    inline constexpr use_future_t use_future{};

    /// Handle used to answer an inbound JSON-RPC request later (deferred reply).
    ///
    /// Request hooks that take a `Responder<Result>` by value and return `void` are dispatched in deferred mode:
    /// the base class does not reply on return, the handler (or whoever it hands the responder to) does, by calling
    /// respond(), fail() or reply() exactly once, possibly from another thread. This is how an agent keeps a
    /// `session/prompt` open while it waits for the client (permission, fs, terminal) or processes `session/cancel`,
    /// and how a client answers `session/request_permission` after a UI click.
    ///
    /// Responders are cheap shared handles (copyable). If the last copy is destroyed without replying, an
    /// `-32603` (Internal error) reply is sent so the peer never waits forever.
    ///
    /// @code
    /// void on_session_prompt(const v1::SessionPromptParams& p, buraq::Responder<v1::SessionPromptResult> done) {
    ///     request_permission(perm_params, done.sink(), [done](auto outcome) mutable {
    ///         done.respond({.stopReason = outcome ? "end_turn" : "refusal"});
    ///     });
    /// }
    /// @endcode
    /// @tparam Result Result type of the request being answered.
    /// @note Thread-safe; the captured sink must be safe to call from whichever thread completes the responder.
    template<typename Result>
    class Responder {
    public:
        /// Result type of the request being answered.
        using result_type = Result;

        /// Creates an empty (invalid) responder.
        Responder() = default;

        /// Binds a responder to an inbound request.
        /// @tparam Codec JSON codec used to encode the reply.
        /// @param id  Id of the request to answer.
        /// @param out Outbound sink; it is copied and kept until the reply is sent.
        /// @return A valid responder.
        template<typename Codec>
        static Responder create(RequestId id, OutboundFn out) {
            Responder r;
            r.state_ = std::make_shared<State>();
            r.state_->id = std::move(id);
            r.state_->out = std::move(out);
            r.state_->encode_result = +[](const RequestId &rid, const Result &value) {
                return Codec::encode_result(rid, value);
            };
            r.state_->encode_error = +[](const RequestId &rid, const RpcError &err) {
                return Codec::encode_error(rid, err);
            };
            return r;
        }

        /// Reports whether this responder is bound to a request.
        [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }
        /// Reports whether a reply has already been sent.
        [[nodiscard]] bool done() const noexcept { return !state_ || state_->done.load(); }
        /// Id of the request being answered. @pre valid()
        [[nodiscard]] const RequestId &id() const noexcept { return state_->id; }

        /// Outbound sink of the connection the request arrived on; use it to stream notifications or send
        /// requests while the reply is pending (e.g. `emit_session_update(update, responder.sink())`).
        /// @pre valid()
        [[nodiscard]] const OutboundFn &sink() const noexcept { return state_->out; }

        /// Sends a success reply.
        /// @param value Result to encode.
        /// @return `true` if this call sent the reply; `false` if one was already sent (or the responder is invalid).
        bool respond(Result value) { return reply(expected<Result, RpcError>(std::move(value))); }

        /// Sends an error reply.
        /// @param error Error to encode.
        /// @return `true` if this call sent the reply; `false` if one was already sent (or the responder is invalid).
        bool fail(RpcError error) { return reply(expected<Result, RpcError>(unexpected(std::move(error)))); }

        /// Sends a success or error reply.
        /// @param outcome Result or error to encode.
        /// @return `true` if this call sent the reply; `false` if one was already sent (or the responder is invalid).
        bool reply(expected<Result, RpcError> outcome) {
            if (!state_ || state_->done.exchange(true)) {
                return false;
            }
            state_->send(outcome
                             ? state_->encode_result(state_->id, *outcome)
                             : state_->encode_error(state_->id, outcome.error()));
            return true;
        }

    private:
        struct State {
            RequestId id{};
            OutboundFn out{};

            std::string (*encode_result)(const RequestId &, const Result &){nullptr};

            std::string (*encode_error)(const RequestId &, const RpcError &){nullptr};

            std::atomic<bool> done{false};

            void send(const std::string &wire) const {
                if (out) out(std::as_bytes(std::span{wire.data(), wire.size()}));
            }

            ~State() {
                if (done.exchange(true)) return;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
                try {
                    send(encode_error(
                        id, RpcError::internal_error("Request handler dropped its Responder without replying")));
                } catch (...) {
                }
#else
                send(encode_error(
                    id, RpcError::internal_error("Request handler dropped its Responder without replying")));
#endif
            }
        };

        std::shared_ptr<State> state_{};
    };

    namespace detail {
        /// Detects a buraq::Responder specialization.
        template<typename T>
        inline constexpr bool is_responder_v = false;
        /// @cond INTERNAL
        template<typename R>
        inline constexpr bool is_responder_v<Responder<R> > = true;
        /// @endcond

        /// Detects the buraq::use_future completion token.
        template<typename T>
        inline constexpr bool is_use_future_v = std::is_same_v<std::remove_cvref_t<T>, use_future_t>;

        /// Invokes a request hook, converting escaping exceptions into `-32603` (Internal error).
        /// @tparam Fn Callable returning `expected<R, RpcError>`.
        /// @param fn Hook invocation.
        /// @return The hook's result, or the converted exception.
        template<typename Fn>
        auto invoke_guarded(Fn &&fn) -> decltype(fn()) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try {
                return fn();
            } catch (const std::exception &ex) {
                return unexpected(RpcError::internal_error(ex.what()));
            } catch (...) {
                return unexpected(RpcError::internal_error());
            }
#else
            return fn();
#endif
        }

        /// Invokes a deferred request hook; an escaping exception fails the responder with `-32603`.
        /// @tparam Result Result type of the request.
        /// @tparam Fn Callable `(Responder<Result>) -> void`.
        /// @param responder Responder handed to the hook (a copy is kept to report exceptions).
        /// @param fn Hook invocation.
        template<typename Result, typename Fn>
        void invoke_deferred(Responder<Result> responder, Fn &&fn) {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
            try {
                fn(responder);
            } catch (const std::exception &ex) {
                responder.fail(RpcError::internal_error(ex.what()));
            } catch (...) {
                responder.fail(RpcError::internal_error());
            }
#else
            fn(responder);
#endif
        }

        /// Thread-safe table of outbound requests awaiting a response, keyed by integer request id.
        class PendingRequests {
        public:
            /// Continuation invoked with the response envelope of a pending request.
            using Handler = std::function<void(const RawEnvelope &)>;

            PendingRequests() = default;

            PendingRequests(const PendingRequests &) = delete;

            PendingRequests &operator=(const PendingRequests &) = delete;

            /// Transfers the pending entries and id counter of @p other.
            PendingRequests(PendingRequests &&other) noexcept {
                std::lock_guard lock(other.mutex_);
                next_id_.store(other.next_id_.load());
                handlers_ = std::move(other.handlers_);
            }

            /// Transfers the pending entries and id counter of @p other.
            PendingRequests &operator=(PendingRequests &&other) noexcept {
                if (this != &other) {
                    std::scoped_lock lock(mutex_, other.mutex_);
                    next_id_.store(other.next_id_.load());
                    handlers_ = std::move(other.handlers_);
                }
                return *this;
            }

            /// Allocates the next request id (starts at 1).
            std::int64_t allocate() noexcept { return next_id_.fetch_add(1); }

            /// Registers @p handler for request @p id.
            void add(std::int64_t id, Handler handler) {
                std::lock_guard lock(mutex_);
                handlers_.insert_or_assign(id, std::move(handler));
            }

            /// Removes the entry for @p id without invoking it.
            /// @return `true` if an entry was removed.
            bool erase(std::int64_t id) {
                std::lock_guard lock(mutex_);
                return handlers_.erase(id) > 0;
            }

            /// Completes the entry matching the response's integer id, invoking its handler outside the lock.
            /// @return `true` if a pending entry matched.
            bool complete(const RawEnvelope &env) {
                if (!env.id || !std::holds_alternative<std::int64_t>(*env.id)) return false;
                Handler handler;
                {
                    std::lock_guard lock(mutex_);
                    auto it = handlers_.find(std::get<std::int64_t>(*env.id));
                    if (it == handlers_.end()) return false;
                    handler = std::move(it->second);
                    handlers_.erase(it);
                }
                handler(env);
                return true;
            }

            /// Fails and clears every entry with @p reason.
            void cancel_all(const RpcError &reason) {
                std::unordered_map<std::int64_t, Handler> pending;
                {
                    std::lock_guard lock(mutex_);
                    pending.swap(handlers_);
                }
                for (auto &[id, handler]: pending) {
                    RawEnvelope err_env{.id = RequestId{id}, .error = reason};
                    handler(err_env);
                }
            }

            /// Number of requests awaiting a response.
            [[nodiscard]] std::size_t size() const {
                std::lock_guard lock(mutex_);
                return handlers_.size();
            }

        private:
            mutable std::mutex mutex_{};
            std::atomic<std::int64_t> next_id_{1};
            std::unordered_map<std::int64_t, Handler> handlers_{};
        };

        /// Allocates an id, registers a typed continuation and emits a request.
        ///
        /// @tparam Result Result type the reply is decoded into.
        /// @tparam Codec  JSON codec.
        /// @param pending  Pending-request table.
        /// @param token    Callback `cb(expected<Result, RpcError>)` or buraq::use_future.
        /// @param on_value Invoked with the decoded result before the callback (e.g. to record capabilities).
        /// @param emit     Invoked as `emit(RequestId)` to encode and write the request.
        /// @return The request id for callbacks, or `std::future<expected<Result, RpcError>>` for buraq::use_future.
        template<typename Result, typename Codec, typename Token, typename OnValue, typename Emit>
        auto send_typed_request(PendingRequests &pending, Token &&token, OnValue on_value, Emit &&emit) {
            using Outcome = expected<Result, RpcError>;
            const auto id = pending.allocate();

            auto register_handler = [&](auto callback) {
                pending.add(
                    id, [callback = std::move(callback), on_value = std::move(on_value)
                    ](const RawEnvelope &env) mutable {
                        if (env.error.has_value()) {
                            callback(Outcome(unexpected(*env.error)));
                            return;
                        }
                        auto decoded = Codec::template decode<Result>(env.result_json);
                        if (decoded) on_value(*decoded);
                        callback(std::move(decoded));
                    });
            };

            auto emit_or_unregister = [&] {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
                try {
                    emit(RequestId{id});
                } catch (...) {
                    pending.erase(id);
                    throw;
                }
#else
                emit(RequestId{id});
#endif
            };

            if constexpr (is_use_future_v<Token>) {
                auto promise = std::make_shared<std::promise<Outcome> >();
                auto future = promise->get_future();
                register_handler([promise](Outcome outcome) { promise->set_value(std::move(outcome)); });
                emit_or_unregister();
                return future;
            } else {
                register_handler(std::decay_t<Token>(std::forward<Token>(token)));
                emit_or_unregister();
                return id;
            }
        }

        /// No-op `on_value` hook for send_typed_request().
        struct ignore_value {
            /// Discards the value.
            template<typename T>
            void operator()(const T &) const noexcept {
            }
        };

        /// Copyable boolean that is safe to read and write from several threads.
        class AtomicFlag {
        public:
            /// Starts `false`.
            AtomicFlag() = default;

            /// Copies the current value of @p other.
            AtomicFlag(const AtomicFlag &other) noexcept : value_(other.value_.load()) {
            }

            /// Assigns the current value of @p other.
            AtomicFlag &operator=(const AtomicFlag &other) noexcept {
                value_.store(other.value_.load());
                return *this;
            }

            /// Stores @p v.
            AtomicFlag &operator=(bool v) noexcept {
                value_.store(v);
                return *this;
            }

            /// Loads the value.
            operator bool() const noexcept { return value_.load(); }

        private:
            std::atomic<bool> value_{false};
        };
    } // namespace detail
} // namespace buraq
