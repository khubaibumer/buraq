#include <map>
#include <string>
#include <variant>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/runtime/connection.hpp>

class EchoAgent : public buraq::AgentBase<EchoAgent, buraq::GlazeCodec> {
    std::map<std::string, std::string> sessions_{}; // sessionId -> cwd
    std::size_t next_session_{1};

public:
    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams &) {
        return buraq::v1::InitializeResult{
            .agentCapabilities = buraq::v1::AgentCapabilities{
                .loadSession = false,
                .promptCapabilities = buraq::v1::PromptCapabilities{
                    .image = false,
                    .audio = false,
                    .embeddedContext = true,
                },
                .sessionCapabilities = buraq::v1::SessionCapabilities{
                    .list = buraq::v1::CapabilityMarker{},
                    .close = buraq::v1::CapabilityMarker{},
                },
            },
            .agentInfo = buraq::v1::ImplementationInfo{"buraq-echo", "0.1.0"},
        };
    }

    buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
    on_session_new(const buraq::v1::SessionNewParams &params) {
        std::string id = "sess_" + std::to_string(next_session_++);
        sessions_.emplace(id, params.cwd);
        return buraq::v1::SessionNewResult{.sessionId = std::move(id)};
    }

    buraq::expected<buraq::v1::SessionListResult, buraq::RpcError>
    on_session_list(const buraq::v1::SessionListParams &params) {
        if (params.cursor) {
            // This agent never paginates, so it never issues cursors.
            return buraq::unexpected(buraq::RpcError::invalid_params("Unknown cursor"));
        }
        buraq::v1::SessionListResult result;
        for (const auto &[id, cwd]: sessions_) {
            if (!params.cwd || *params.cwd == cwd) {
                result.sessions.push_back({.sessionId = id, .cwd = cwd});
            }
        }
        return result;
    }

    buraq::expected<buraq::v1::SessionCloseResult, buraq::RpcError>
    on_session_close(const buraq::v1::SessionCloseParams &params) {
        sessions_.erase(params.sessionId); // idempotent: closing an unknown session also succeeds
        return buraq::v1::SessionCloseResult{};
    }

    template<typename Sink>
    buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
    on_session_prompt(const buraq::v1::SessionPromptParams &params, Sink &sink) {
        if (!sessions_.contains(params.sessionId)) {
            return buraq::unexpected(buraq::RpcError::session_not_found(params.sessionId));
        }

        std::string reply;
        for (const auto &block: params.prompt) {
            if (const auto *text = std::get_if<buraq::v1::TextContent>(&block)) {
                if (!reply.empty()) reply += '\n';
                reply += text->text;
            }
        }
        if (reply.empty()) {
            reply = "Echo from buraq C++20/23!";
        }

        emit_session_update(buraq::v1::SessionUpdateNotification{
                                .sessionId = params.sessionId,
                                .update = buraq::v1::AgentMessageChunk{
                                    .content = buraq::v1::TextContent{.text = std::move(reply)},
                                },
                            }, sink);

        return buraq::v1::SessionPromptResult{.stopReason = "end_turn"};
    }

    void on_session_cancel(const buraq::v1::SessionCancelParams &) {
        // Echo replies synchronously, so there is no in-flight turn to abort.
    }
};

int main() {
    EchoAgent agent;
    return buraq::runtime::serve_stdio(agent);
}
