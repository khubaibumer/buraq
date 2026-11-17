#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace buraq::v1 {
    inline constexpr std::uint32_t kProtocolVersion = 1;

    struct ClientCapabilities {
        struct FsCapabilities {
            bool readTextFile{false};
            bool writeTextFile{false};
        };

        std::optional<FsCapabilities> fs{};
        bool terminal{false};
    };

    struct AgentCapabilities {
        bool loadSession{false};

        struct PromptCapabilities {
            bool image{false};
            bool audio{false};
            bool embeddedContext{false};
        };

        std::optional<PromptCapabilities> promptCapabilities{};
    };

    struct ImplementationInfo {
        std::string name;
        std::string version;
        std::optional<std::string> title{};
    };

    // --- Lifecycle: initialize ---
    struct InitializeParams {
        std::uint32_t protocolVersion{kProtocolVersion};
        ClientCapabilities clientCapabilities{};
        std::optional<ImplementationInfo> clientInfo{};
    };

    struct InitializeResult {
        std::uint32_t protocolVersion{kProtocolVersion};
        AgentCapabilities agentCapabilities{};
        std::optional<ImplementationInfo> agentInfo{};
    };

    // --- Content Blocks (tagged union discriminated by "type") ---
    struct TextContent {
        std::string_view type{"text"};
        std::string text;
    };

    struct ResourceLinkContent {
        std::string_view type{"resource_link"};
        std::string uri;
        std::string name;
        std::optional<std::string> mimeType{};
    };

    using ContentBlock = std::variant<TextContent, ResourceLinkContent>;

    // --- Sessions: session/new, session/prompt, session/cancel ---
    struct McpServerStdio {
        std::string name;
        std::string command;
        std::vector<std::string> args{};
    };

    struct SessionNewParams {
        std::string cwd;
        std::vector<McpServerStdio> mcpServers{};
    };

    struct SessionNewResult {
        std::string sessionId;
    };

    struct SessionPromptParams {
        std::string sessionId;
        std::vector<ContentBlock> prompt;
    };

    enum class StopReason {
        end_turn,
        max_tokens,
        max_turn_requests,
        refusal,
        cancelled
    };

    struct SessionPromptResult {
        std::string stopReason{"end_turn"};
    };

    struct SessionCancelParams {
        std::string sessionId;
    };

    // --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
    struct AgentMessageChunk {
        std::string_view sessionUpdate{"agent_message_chunk"};
        TextContent content;
    };

    struct ToolCallUpdate {
        std::string_view sessionUpdate{"tool_call"};
        std::string toolCallId;
        std::string title;
        std::string status; // "pending", "in_progress", "completed", "failed"
    };

    using SessionUpdatePayload = std::variant<AgentMessageChunk, ToolCallUpdate>;

    struct SessionUpdateNotification {
        std::string sessionId;
        SessionUpdatePayload update;
    };

    // --- Client-Side Delegated Tools (Agent -> Client requests) ---
    struct FsReadTextFileParams {
        std::string sessionId;
        std::string path;
        std::optional<std::uint32_t> line{};
        std::optional<std::uint32_t> limit{};
    };

    struct FsReadTextFileResult {
        std::string content;
    };

    struct FsWriteTextFileParams {
        std::string sessionId;
        std::string path;
        std::string content;
    };

    struct FsWriteTextFileResult {
    };
} // namespace buraq::v1
