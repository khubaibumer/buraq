#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace buraq::v1 {

inline constexpr std::uint32_t kProtocolVersion = 1;

// --- Capabilities & Metadata ---
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

struct ImageContent {
    std::string_view type{"image"};
    std::string data;     // Base64-encoded image bytes
    std::string mimeType; // e.g., "image/png"
    std::optional<std::string> uri{};
};

struct AudioContent {
    std::string_view type{"audio"};
    std::string data;     // Base64-encoded audio bytes
    std::string mimeType; // e.g., "audio/wav"
};

struct ResourceLinkContent {
    std::string_view type{"resource_link"};
    std::string uri;
    std::string name;
    std::optional<std::string> mimeType{};
    std::optional<std::string> description{};
};

struct EmbeddedResourcePayload {
    std::string uri;
    std::optional<std::string> mimeType{};
    std::optional<std::string> text{};
    std::optional<std::string> blob{}; // Base64 when binary
};

struct EmbeddedResourceContent {
    std::string_view type{"resource"};
    EmbeddedResourcePayload resource;
};

using ContentBlock = std::variant<
    TextContent,
    ImageContent,
    AudioContent,
    ResourceLinkContent,
    EmbeddedResourceContent
>;

// --- Sessions: new, load, mode, model, prompt, cancel ---
struct EnvVariable {
    std::string name;
    std::string value;
};

struct McpServerStdio {
    std::string name;
    std::string command;
    std::vector<std::string> args{};
    std::vector<EnvVariable> env{};
};

struct SessionMode {
    std::string id;
    std::string name;
    std::optional<std::string> description{};
};

struct SessionModeState {
    std::string currentModeId;
    std::vector<SessionMode> availableModes{};
};

struct ModelInfo {
    std::string modelId;
    std::string name;
    std::optional<std::string> description{};
};

struct SessionModelState {
    std::string currentModelId;
    std::vector<ModelInfo> availableModels{};
};

struct SessionNewParams {
    std::string cwd;
    std::vector<McpServerStdio> mcpServers{};
};

struct SessionNewResult {
    std::string sessionId;
    std::optional<SessionModeState> modes{};
    std::optional<SessionModelState> models{};
};

struct SessionLoadParams {
    std::string sessionId;
    std::string cwd;
    std::vector<McpServerStdio> mcpServers{};
};

struct SessionLoadResult {
    std::optional<SessionModeState> modes{};
    std::optional<SessionModelState> models{};
};

struct SessionSetModeParams {
    std::string sessionId;
    std::string modeId;
};

struct SessionSetModeResult {};

struct SessionSetModelParams {
    std::string sessionId;
    std::string modelId;
};

struct SessionSetModelResult {};

struct SessionPromptParams {
    std::string sessionId;
    std::vector<ContentBlock> prompt;
};

struct SessionPromptResult {
    std::string stopReason{"end_turn"}; // "end_turn", "max_tokens", "max_turn_requests", "refusal", "cancelled"
};

struct SessionCancelParams {
    std::string sessionId;
};

// --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
struct UserMessageChunk {
    std::string_view sessionUpdate{"user_message_chunk"};
    ContentBlock content;
};

struct AgentMessageChunk {
    std::string_view sessionUpdate{"agent_message_chunk"};
    ContentBlock content;
};

struct AgentThoughtChunk {
    std::string_view sessionUpdate{"agent_thought_chunk"};
    ContentBlock content;
};

struct ToolCallLocation {
    std::string path;
    std::optional<std::uint32_t> line{};
};

struct ToolCallContentItem {
    std::string_view type{"content"};
    ContentBlock content;
};

struct ToolCallDiffItem {
    std::string_view type{"diff"};
    std::string path;
    std::optional<std::string> oldText{};
    std::string newText;
};

struct ToolCallTerminalItem {
    std::string_view type{"terminal"};
    std::string terminalId;
};

using ToolCallContent = std::variant<
    ToolCallContentItem,
    ToolCallDiffItem,
    ToolCallTerminalItem
>;

struct ToolCallUpdate {
    std::string_view sessionUpdate{"tool_call"};
    std::string toolCallId;
    std::string title;
    std::optional<std::string> kind{};   // "read", "edit", "delete", "move", "search", "execute", "think", "fetch", "other"
    std::optional<std::string> status{}; // "pending", "in_progress", "completed", "failed"
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
};

struct ToolCallStatusUpdate {
    std::string_view sessionUpdate{"tool_call_update"};
    std::string toolCallId;
    std::optional<std::string> title{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
};

struct PlanEntry {
    std::string content;
    std::string priority; // "high", "medium", "low"
    std::string status;   // "pending", "in_progress", "completed"
};

struct PlanUpdate {
    std::string_view sessionUpdate{"plan"};
    std::vector<PlanEntry> entries{};
};

struct AvailableCommand {
    std::string name;
    std::string description;
    std::optional<std::string> inputHint{};
};

struct AvailableCommandsUpdate {
    std::string_view sessionUpdate{"available_commands_update"};
    std::vector<AvailableCommand> availableCommands{};
};

using SessionUpdatePayload = std::variant<
    UserMessageChunk,
    AgentMessageChunk,
    AgentThoughtChunk,
    ToolCallUpdate,
    ToolCallStatusUpdate,
    PlanUpdate,
    AvailableCommandsUpdate
>;

struct SessionUpdateNotification {
    std::string sessionId;
    SessionUpdatePayload update;
};

// --- Permissions: session/request_permission (Agent -> Client request) ---
struct PermissionOption {
    std::string optionId;
    std::string name;
    std::string kind; // "allow_once", "allow_always", "reject_once", "reject_always"
};

struct PermissionToolCallInfo {
    std::string toolCallId;
    std::optional<std::string> title{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
};

struct RequestPermissionParams {
    std::string sessionId;
    PermissionToolCallInfo toolCall;
    std::vector<PermissionOption> options{};
};

struct PermissionCancelledOutcome {
    std::string_view outcome{"cancelled"};
};

struct PermissionSelectedOutcome {
    std::string_view outcome{"selected"};
    std::string optionId;
};

using PermissionOutcome = std::variant<
    PermissionCancelledOutcome,
    PermissionSelectedOutcome
>;

struct RequestPermissionResult {
    PermissionOutcome outcome;
};

// --- Delegated Filesystem Tools (Agent -> Client requests) ---
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

struct FsWriteTextFileResult {};

// --- Delegated Terminal Tools (Agent -> Client requests) ---
struct TerminalCreateParams {
    std::string sessionId;
    std::string command;
    std::vector<std::string> args{};
    std::optional<std::string> cwd{};
    std::vector<EnvVariable> env{};
    std::optional<std::uint64_t> outputByteLimit{};
};

struct TerminalCreateResult {
    std::string terminalId;
};

struct TerminalOutputParams {
    std::string sessionId;
    std::string terminalId;
};

struct TerminalExitStatus {
    std::optional<int> exitCode{};
    std::optional<std::string> signal{};
};

struct TerminalOutputResult {
    std::string output;
    bool truncated{false};
    std::optional<TerminalExitStatus> exitStatus{};
};

struct TerminalWaitForExitParams {
    std::string sessionId;
    std::string terminalId;
};

struct TerminalWaitForExitResult {
    std::optional<int> exitCode{};
    std::optional<std::string> signal{};
};

struct TerminalKillParams {
    std::string sessionId;
    std::string terminalId;
};

struct TerminalKillResult {};

struct TerminalReleaseParams {
    std::string sessionId;
    std::string terminalId;
};

struct TerminalReleaseResult {};

} // namespace buraq::v1