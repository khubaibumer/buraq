#include <string>
#include <unordered_set>
#include <variant>

#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/transport/stdio.hpp>

class EchoAgent : public buraq::AgentBase<EchoAgent, buraq::GlazeCodec> {
    std::unordered_set<std::string> sessions_{};
    std::size_t next_session_{1};

public:
    buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
    on_initialize(const buraq::v1::InitializeParams&) {
        return buraq::v1::InitializeResult{
            .agentCapabilities = buraq::v1::AgentCapabilities{
                .loadSession = false,
                .promptCapabilities = buraq::v1::AgentCapabilities::PromptCapabilities{
                    .image = false,
                    .audio = false,
                    .embeddedContext = true,
                },
            },
            .agentInfo = buraq::v1::ImplementationInfo{"buraq-echo", "0.1.0"},
        };
    }

    buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
    on_session_new(const buraq::v1::SessionNewParams&) {
        std::string id = "sess_" + std::to_string(next_session_++);
        sessions_.insert(id);
        return buraq::v1::SessionNewResult{.sessionId = std::move(id)};
    }

    template <typename Sink>
    buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
    on_session_prompt(const buraq::v1::SessionPromptParams& params, Sink& sink) {
        if (!sessions_.contains(params.sessionId)) {
            return buraq::unexpected(buraq::RpcError::invalid_params("Unknown sessionId"));
        }

        std::string reply;
        for (const auto& block : params.prompt) {
            if (const auto* text = std::get_if<buraq::v1::TextContent>(&block)) {
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

    void on_session_cancel(const buraq::v1::SessionCancelParams&) {
        // Acknowledge cancellation cleanly
    }
};

int main() {
    EchoAgent agent;
    return buraq::transport::run_stdio_agent(agent);
}