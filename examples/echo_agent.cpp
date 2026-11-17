#include <buraq/acp.hpp>
#include <buraq/codecs/glaze.hpp>
#include <buraq/transport/stdio.hpp>

class EchoAgent : public buraq::AgentBase<EchoAgent, buraq::GlazeCodec> {
public:
  buraq::expected<buraq::v1::InitializeResult, buraq::RpcError>
  on_initialize(const buraq::v1::InitializeParams&) {
    return buraq::v1::InitializeResult{
      .agentInfo = buraq::v1::ImplementationInfo{"buraq-echo", "0.1.0"}
    };
  }

  buraq::expected<buraq::v1::SessionNewResult, buraq::RpcError>
  on_session_new(const buraq::v1::SessionNewParams&) {
    return buraq::v1::SessionNewResult{.sessionId = "sess_1"};
  }

  template <typename Sink>
  buraq::expected<buraq::v1::SessionPromptResult, buraq::RpcError>
  on_session_prompt(const buraq::v1::SessionPromptParams& params, Sink& sink) {
    emit_session_update(buraq::v1::SessionUpdateNotification{
        .sessionId = params.sessionId,
        .update = buraq::v1::AgentMessageChunk{
            .content = buraq::v1::TextContent{.text = "Echo from buraq C++20/23!"}
        }
    }, sink);

    return buraq::v1::SessionPromptResult{.stopReason = "end_turn"};
  }
};

int main() {
  EchoAgent agent;
  return buraq::transport::run_stdio_agent(agent);
}