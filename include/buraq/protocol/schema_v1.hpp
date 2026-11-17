#pragma once

#include <compare>
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
        auto operator<=>(const FsCapabilities&) const = default;
    };
    std::optional<FsCapabilities> fs{};
    bool terminal{false};
    auto operator<=>(const ClientCapabilities&) const = default;
};

struct AgentCapabilities {
    bool loadSession{false};
    struct PromptCapabilities {
        bool image{false};
        bool audio{false};
        bool embeddedContext{false};
        auto operator<=>(const PromptCapabilities&) const = default;
    };
    std::optional<PromptCapabilities> promptCapabilities{};
    auto operator<=>(const AgentCapabilities&) const = default;
};

struct ImplementationInfo {
    std::string name;
    std::string version;
    std::optional<std::string> title{};
    auto operator<=>(const ImplementationInfo&) const = default;
};

// --- Lifecycle: initialize ---
struct InitializeParams {
    std::uint32_t protocolVersion{kProtocolVersion};
    ClientCapabilities clientCapabilities{};
    std::optional<ImplementationInfo> clientInfo{};
    auto operator<=>(const InitializeParams&) const = default;
};

struct InitializeResult {
    std::uint32_t protocolVersion{kProtocolVersion};
    AgentCapabilities agentCapabilities{};
    std::optional<ImplementationInfo> agentInfo{};
    auto operator<=>(const InitializeResult&) const = default;
};

// --- Content Blocks (tagged union discriminated by "type") ---
struct TextContent {
    std::string_view type{"text"};
    std::string text;
    auto operator<=>(const TextContent&) const = default;
};

struct ImageContent {
    std::string_view type{"image"};
    std::string data;     // Base64-encoded image bytes
    std::string mimeType; // e.g., "image/png"
    std::optional<std::string> uri{};
    auto operator<=>(const ImageContent&) const = default;
};

struct AudioContent {
    std::string_view type{"audio"};
    std::string data;     // Base64-encoded audio bytes
    std::string mimeType; // e.g., "audio/wav"
    auto operator<=>(const AudioContent&) const = default;
};

struct ResourceLinkContent {
    std::string_view type{"resource_link"};
    std::string uri;
    std::string name;
    std::optional<std::string> mimeType{};
    std::optional<std::string> description{};
    auto operator<=>(const ResourceLinkContent&) const = default;
};

struct EmbeddedResourcePayload {
    std::string uri;
    std::optional<std::string> mimeType{};
    std::optional<std::string> text{};
    std::optional<std::string> blob{}; // Base64 when binary
    auto operator<=>(const EmbeddedResourcePayload&) const = default;
};

struct EmbeddedResourceContent {
    std::string_view type{"resource"};
    EmbeddedResourcePayload resource;
    auto operator<=>(const EmbeddedResourceContent&) const = default;
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
    auto operator<=>(const EnvVariable&) const = default;
};

struct McpServerStdio {
    std::string name;
    std::string command;
    std::vector<std::string> args{};
    std::vector<EnvVariable> env{};
    auto operator<=>(const McpServerStdio&) const = default;
};

struct SessionMode {
    std::string id;
    std::string name;
    std::optional<std::string> description{};
    auto operator<=>(const SessionMode&) const = default;
};

struct SessionModeState {
    std::string currentModeId;
    std::vector<SessionMode> availableModes{};
    auto operator<=>(const SessionModeState&) const = default;
};

struct ModelInfo {
    std::string modelId;
    std::string name;
    std::optional<std::string> description{};
    auto operator<=>(const ModelInfo&) const = default;
};

struct SessionModelState {
    std::string currentModelId;
    std::vector<ModelInfo> availableModels{};
    auto operator<=>(const SessionModelState&) const = default;
};

struct SessionNewParams {
    std::string cwd;
    std::vector<McpServerStdio> mcpServers{};
    auto operator<=>(const SessionNewParams&) const = default;
};

struct SessionNewResult {
    std::string sessionId;
    std::optional<SessionModeState> modes{};
    std::optional<SessionModelState> models{};
    auto operator<=>(const SessionNewResult&) const = default;
};

struct SessionLoadParams {
    std::string sessionId;
    std::string cwd;
    std::vector<McpServerStdio> mcpServers{};
    auto operator<=>(const SessionLoadParams&) const = default;
};

struct SessionLoadResult {
    std::optional<SessionModeState> modes{};
    std::optional<SessionModelState> models{};
    auto operator<=>(const SessionLoadResult&) const = default;
};

struct SessionSetModeParams {
    std::string sessionId;
    std::string modeId;
    auto operator<=>(const SessionSetModeParams&) const = default;
};

struct SessionSetModeResult {
    auto operator<=>(const SessionSetModeResult&) const = default;
};

struct SessionSetModelParams {
    std::string sessionId;
    std::string modelId;
    auto operator<=>(const SessionSetModelParams&) const = default;
};

struct SessionSetModelResult {
    auto operator<=>(const SessionSetModelResult&) const = default;
};

struct SessionPromptParams {
    std::string sessionId;
    std::vector<ContentBlock> prompt;
    auto operator<=>(const SessionPromptParams&) const = default;
};

struct SessionPromptResult {
    std::string stopReason{"end_turn"}; // "end_turn", "max_tokens", "max_turn_requests", "refusal", "cancelled"
    auto operator<=>(const SessionPromptResult&) const = default;
};

struct SessionCancelParams {
    std::string sessionId;
    auto operator<=>(const SessionCancelParams&) const = default;
};

// --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
struct UserMessageChunk {
    std::string_view sessionUpdate{"user_message_chunk"};
    ContentBlock content;
    auto operator<=>(const UserMessageChunk&) const = default;
};

struct AgentMessageChunk {
    std::string_view sessionUpdate{"agent_message_chunk"};
    ContentBlock content;
    auto operator<=>(const AgentMessageChunk&) const = default;
};

struct AgentThoughtChunk {
    std::string_view sessionUpdate{"agent_thought_chunk"};
    ContentBlock content;
    auto operator<=>(const AgentThoughtChunk&) const = default;
};

struct ToolCallLocation {
    std::string path;
    std::optional<std::uint32_t> line{};
    auto operator<=>(const ToolCallLocation&) const = default;
};

struct ToolCallContentItem {
    std::string_view type{"content"};
    ContentBlock content;
    auto operator<=>(const ToolCallContentItem&) const = default;
};

struct ToolCallDiffItem {
    std::string_view type{"diff"};
    std::string path;
    std::optional<std::string> oldText{};
    std::string newText;
    auto operator<=>(const ToolCallDiffItem&) const = default;
};

struct ToolCallTerminalItem {
    std::string_view type{"terminal"};
    std::string terminalId;
    auto operator<=>(const ToolCallTerminalItem&) const = default;
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
    auto operator<=>(const ToolCallUpdate&) const = default;
};

struct ToolCallStatusUpdate {
    std::string_view sessionUpdate{"tool_call_update"};
    std::string toolCallId;
    std::optional<std::string> title{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
    auto operator<=>(const ToolCallStatusUpdate&) const = default;
};

struct PlanEntry {
    std::string content;
    std::string priority; // "high", "medium", "low"
    std::string status;   // "pending", "in_progress", "completed"
    auto operator<=>(const PlanEntry&) const = default;
};

struct PlanUpdate {
    std::string_view sessionUpdate{"plan"};
    std::vector<PlanEntry> entries{};
    auto operator<=>(const PlanUpdate&) const = default;
};

struct AvailableCommand {
    std::string name;
    std::string description;
    std::optional<std::string> inputHint{};
    auto operator<=>(const AvailableCommand&) const = default;
};

struct AvailableCommandsUpdate {
    std::string_view sessionUpdate{"available_commands_update"};
    std::vector<AvailableCommand> availableCommands{};
    auto operator<=>(const AvailableCommandsUpdate&) const = default;
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
    auto operator<=>(const SessionUpdateNotification&) const = default;
};

// --- Permissions: session/request_permission (Agent -> Client request) ---
struct PermissionOption {
    std::string optionId;
    std::string name;
    std::string kind; // "allow_once", "allow_always", "reject_once", "reject_always"
    auto operator<=>(const PermissionOption&) const = default;
};

struct PermissionToolCallInfo {
    std::string toolCallId;
    std::optional<std::string> title{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
    auto operator<=>(const PermissionToolCallInfo&) const = default;
};

struct RequestPermissionParams {
    std::string sessionId;
    PermissionToolCallInfo toolCall;
    std::vector<PermissionOption> options{};
    auto operator<=>(const RequestPermissionParams&) const = default;
};

struct PermissionCancelledOutcome {
    std::string_view outcome{"cancelled"};
    auto operator<=>(const PermissionCancelledOutcome&) const = default;
};

struct PermissionSelectedOutcome {
    std::string_view outcome{"selected"};
    std::string optionId;
    auto operator<=>(const PermissionSelectedOutcome&) const = default;
};

using PermissionOutcome = std::variant<
    PermissionCancelledOutcome,
    PermissionSelectedOutcome
>;

struct RequestPermissionResult {
    PermissionOutcome outcome;
    auto operator<=>(const RequestPermissionResult&) const = default;
};

// --- Delegated Filesystem Tools (Agent -> Client requests) ---
struct FsReadTextFileParams {
    std::string sessionId;
    std::string path;
    std::optional<std::uint32_t> line{};
    std::optional<std::uint32_t> limit{};
    auto operator<=>(const FsReadTextFileParams&) const = default;
};

struct FsReadTextFileResult {
    std::string content;
    auto operator<=>(const FsReadTextFileResult&) const = default;
};

struct FsWriteTextFileParams {
    std::string sessionId;
    std::string path;
    std::string content;
    auto operator<=>(const FsWriteTextFileParams&) const = default;
};

struct FsWriteTextFileResult {
    auto operator<=>(const FsWriteTextFileResult&) const = default;
};

// --- Delegated Terminal Tools (Agent -> Client requests) ---
struct TerminalCreateParams {
    std::string sessionId;
    std::string command;
    std::vector<std::string> args{};
    std::optional<std::string> cwd{};
    std::vector<EnvVariable> env{};
    std::optional<std::uint64_t> outputByteLimit{};
    auto operator<=>(const TerminalCreateParams&) const = default;
};

struct TerminalCreateResult {
    std::string terminalId;
    auto operator<=>(const TerminalCreateResult&) const = default;
};

struct TerminalOutputParams {
    std::string sessionId;
    std::string terminalId;
    auto operator<=>(const TerminalOutputParams&) const = default;
};

struct TerminalExitStatus {
    std::optional<int> exitCode{};
    std::optional<std::string> signal{};
    auto operator<=>(const TerminalExitStatus&) const = default;
};

struct TerminalOutputResult {
    std::string output;
    bool truncated{false};
    std::optional<TerminalExitStatus> exitStatus{};
    auto operator<=>(const TerminalOutputResult&) const = default;
};

struct TerminalWaitForExitParams {
    std::string sessionId;
    std::string terminalId;
    auto operator<=>(const TerminalWaitForExitParams&) const = default;
};

struct TerminalWaitForExitResult {
    std::optional<int> exitCode{};
    std::optional<std::string> signal{};
    auto operator<=>(const TerminalWaitForExitResult&) const = default;
};

struct TerminalKillParams {
    std::string sessionId;
    std::string terminalId;
    auto operator<=>(const TerminalKillParams&) const = default;
};

struct TerminalKillResult {
    auto operator<=>(const TerminalKillResult&) const = default;
};

struct TerminalReleaseParams {
    std::string sessionId;
    std::string terminalId;
    auto operator<=>(const TerminalReleaseParams&) const = default;
};

struct TerminalReleaseResult {
    auto operator<=>(const TerminalReleaseResult&) const = default;
};

} // namespace buraq::v1