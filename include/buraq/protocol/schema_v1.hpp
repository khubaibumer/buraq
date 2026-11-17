#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "buraq/protocol/jsonrpc.hpp"

// ACP v1 protocol types. Field names mirror the wire format (camelCase) so codecs can reflect them
// directly. Open string enums (stop reasons, tool kinds, statuses, ...) are kept as std::string so
// values introduced by newer protocol revisions round-trip instead of failing to decode.
namespace buraq::v1 {

inline constexpr std::uint32_t kProtocolVersion = 1;

// Presence-only capability object (`{}` on the wire means "supported")
struct CapabilityMarker {
    auto operator<=>(const CapabilityMarker&) const = default;
};

// --- Capabilities & Metadata ---
struct FileSystemCapabilities {
    bool readTextFile{false};
    bool writeTextFile{false};
    auto operator<=>(const FileSystemCapabilities&) const = default;
};

struct SessionConfigOptionsCapabilities {
    std::optional<CapabilityMarker> boolean{};
    auto operator<=>(const SessionConfigOptionsCapabilities&) const = default;
};

struct ClientSessionCapabilities {
    std::optional<CapabilityMarker> compaction{};
    std::optional<SessionConfigOptionsCapabilities> configOptions{};
    std::optional<CapabilityMarker> notices{};
    auto operator<=>(const ClientSessionCapabilities&) const = default;
};

struct ClientAuthCapabilities {
    bool terminal{false};
    auto operator<=>(const ClientAuthCapabilities&) const = default;
};

struct ElicitationCapabilities {
    std::optional<CapabilityMarker> form{};
    std::optional<CapabilityMarker> url{};
    auto operator<=>(const ElicitationCapabilities&) const = default;
};

struct ClientCapabilities {
    using FsCapabilities = FileSystemCapabilities;
    std::optional<FileSystemCapabilities> fs{};
    bool terminal{false};
    std::optional<ClientSessionCapabilities> session{};
    std::optional<ClientAuthCapabilities> auth{};
    std::optional<ElicitationCapabilities> elicitation{};
    auto operator<=>(const ClientCapabilities&) const = default;
};

struct PromptCapabilities {
    bool image{false};
    bool audio{false};
    bool embeddedContext{false};
    auto operator<=>(const PromptCapabilities&) const = default;
};

struct McpCapabilities {
    bool http{false};
    bool sse{false};
    auto operator<=>(const McpCapabilities&) const = default;
};

struct SystemPromptModeCapabilities {
    std::optional<CapabilityMarker> override{};
    auto operator<=>(const SystemPromptModeCapabilities&) const = default;
};

struct SystemPromptCapabilities {
    std::optional<SystemPromptModeCapabilities> mode{};
    auto operator<=>(const SystemPromptCapabilities&) const = default;
};

// `delete_` is serialized as "delete" (a C++ keyword)
struct SessionCapabilities {
    std::optional<CapabilityMarker> list{};
    std::optional<CapabilityMarker> delete_{};
    std::optional<CapabilityMarker> additionalDirectories{};
    std::optional<SystemPromptCapabilities> systemPrompt{};
    std::optional<CapabilityMarker> resume{};
    std::optional<CapabilityMarker> close{};
    auto operator<=>(const SessionCapabilities&) const = default;
};

struct AgentAuthCapabilities {
    std::optional<CapabilityMarker> logout{};
    auto operator<=>(const AgentAuthCapabilities&) const = default;
};

struct AgentCapabilities {
    using PromptCapabilities = ::buraq::v1::PromptCapabilities;
    bool loadSession{false};
    std::optional<PromptCapabilities> promptCapabilities{};
    std::optional<McpCapabilities> mcpCapabilities{};
    std::optional<SessionCapabilities> sessionCapabilities{};
    std::optional<AgentAuthCapabilities> auth{};
    auto operator<=>(const AgentCapabilities&) const = default;
};

struct ImplementationInfo {
    std::string name;
    std::string version;
    std::optional<std::string> title{};
    auto operator<=>(const ImplementationInfo&) const = default;
};

// --- Authentication (tagged union discriminated by "type"; untagged = agent-handled) ---
struct AuthMethodAgent {
    std::string id;
    std::string name;
    std::optional<std::string> description{};
    auto operator<=>(const AuthMethodAgent&) const = default;
};

struct AuthMethodTerminal {
    std::string_view type{"terminal"};
    std::string id;
    std::string name;
    std::optional<std::string> description{};
    std::optional<std::vector<std::string>> args{};
    std::optional<std::map<std::string, std::string>> env{};
    auto operator<=>(const AuthMethodTerminal&) const = default;
};

using AuthMethod = std::variant<
    AuthMethodAgent,
    AuthMethodTerminal
>;

struct AuthenticateParams {
    std::string methodId;
    auto operator<=>(const AuthenticateParams&) const = default;
};

struct AuthenticateResult {
    auto operator<=>(const AuthenticateResult&) const = default;
};

struct LogoutParams {
    auto operator<=>(const LogoutParams&) const = default;
};

struct LogoutResult {
    auto operator<=>(const LogoutResult&) const = default;
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
    std::vector<AuthMethod> authMethods{};
    std::optional<ImplementationInfo> agentInfo{};
    auto operator<=>(const InitializeResult&) const = default;
};

// --- Content Blocks (tagged union discriminated by "type") ---
struct Annotations {
    std::optional<std::vector<std::string>> audience{}; // "user", "assistant"
    std::optional<std::string> lastModified{};
    std::optional<double> priority{};
    auto operator<=>(const Annotations&) const = default;
};

struct TextContent {
    std::string_view type{"text"};
    std::string text;
    std::optional<Annotations> annotations{};
    auto operator<=>(const TextContent&) const = default;
};

struct ImageContent {
    std::string_view type{"image"};
    std::string data;     // Base64-encoded image bytes
    std::string mimeType; // e.g., "image/png"
    std::optional<std::string> uri{};
    std::optional<Annotations> annotations{};
    auto operator<=>(const ImageContent&) const = default;
};

struct AudioContent {
    std::string_view type{"audio"};
    std::string data;     // Base64-encoded audio bytes
    std::string mimeType; // e.g., "audio/wav"
    std::optional<Annotations> annotations{};
    auto operator<=>(const AudioContent&) const = default;
};

struct ResourceLinkContent {
    std::string_view type{"resource_link"};
    std::string uri;
    std::string name;
    std::optional<std::string> mimeType{};
    std::optional<std::string> description{};
    std::optional<std::string> title{};
    std::optional<std::int64_t> size{};
    std::optional<Annotations> annotations{};
    auto operator<=>(const ResourceLinkContent&) const = default;
};

// Text (`text`) or binary (`blob`, Base64) resource contents
struct EmbeddedResourcePayload {
    std::string uri;
    std::optional<std::string> mimeType{};
    std::optional<std::string> text{};
    std::optional<std::string> blob{};
    auto operator<=>(const EmbeddedResourcePayload&) const = default;
};

struct EmbeddedResourceContent {
    std::string_view type{"resource"};
    EmbeddedResourcePayload resource;
    std::optional<Annotations> annotations{};
    auto operator<=>(const EmbeddedResourceContent&) const = default;
};

using ContentBlock = std::variant<
    TextContent,
    ImageContent,
    AudioContent,
    ResourceLinkContent,
    EmbeddedResourceContent
>;

// --- MCP servers (tagged union discriminated by "type"; untagged = stdio) ---
struct EnvVariable {
    std::string name;
    std::string value;
    auto operator<=>(const EnvVariable&) const = default;
};

struct HttpHeader {
    std::string name;
    std::string value;
    auto operator<=>(const HttpHeader&) const = default;
};

struct McpServerHttp {
    std::string_view type{"http"};
    std::string name;
    std::string url;
    std::vector<HttpHeader> headers{};
    auto operator<=>(const McpServerHttp&) const = default;
};

struct McpServerSse {
    std::string_view type{"sse"};
    std::string name;
    std::string url;
    std::vector<HttpHeader> headers{};
    auto operator<=>(const McpServerSse&) const = default;
};

struct McpServerStdio {
    std::string name;
    std::string command;
    std::vector<std::string> args{};
    std::vector<EnvVariable> env{};
    auto operator<=>(const McpServerStdio&) const = default;
};

using McpServer = std::variant<
    McpServerHttp,
    McpServerSse,
    McpServerStdio
>;

// --- Session configuration ---
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

// UNSTABLE: session models are not part of the stable ACP v1 schema
struct ModelInfo {
    std::string modelId;
    std::string name;
    std::optional<std::string> description{};
    auto operator<=>(const ModelInfo&) const = default;
};

// UNSTABLE: session models are not part of the stable ACP v1 schema
struct SessionModelState {
    std::string currentModelId;
    std::vector<ModelInfo> availableModels{};
    auto operator<=>(const SessionModelState&) const = default;
};

// Value of a config option: a value id (`type: "select"`) or a boolean (`type: "boolean"`)
using SessionConfigValue = std::variant<std::string, bool>;

struct SessionConfigSelectOption {
    std::string value;
    std::string name;
    std::optional<std::string> description{};
    auto operator<=>(const SessionConfigSelectOption&) const = default;
};

// Entry of a select option list: either an option (`value` set) or a group (`group` + `options` set)
struct SessionConfigSelectItem {
    std::optional<std::string> value{};
    std::string name;
    std::optional<std::string> description{};
    std::optional<std::string> group{};
    std::optional<std::vector<SessionConfigSelectOption>> options{};

    [[nodiscard]] bool is_group() const noexcept { return group.has_value(); }
    auto operator<=>(const SessionConfigSelectItem&) const = default;
};

struct SessionConfigOption {
    std::string id;
    std::string name;
    std::optional<std::string> description{};
    std::optional<std::string> category{}; // "mode", "model", "model_config", "thought_level", ...
    std::string type{"select"};            // "select" or "boolean"
    SessionConfigValue currentValue{std::string{}};
    std::optional<std::vector<SessionConfigSelectItem>> options{}; // present for "select"
    auto operator<=>(const SessionConfigOption&) const = default;
};

struct SystemPrompt {
    std::string mode{"append"}; // "append", "override"
    std::vector<ContentBlock> content{};
    auto operator<=>(const SystemPrompt&) const = default;
};

// --- Sessions: new, load, resume, list, delete, close, mode, config, prompt, cancel ---
struct SessionNewParams {
    std::string cwd;
    std::vector<McpServer> mcpServers{};
    std::optional<std::vector<std::string>> additionalDirectories{};
    std::optional<SystemPrompt> systemPrompt{};
    auto operator<=>(const SessionNewParams&) const = default;
};

struct SessionNewResult {
    std::string sessionId;
    std::optional<SessionModeState> modes{};
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    std::optional<SessionModelState> models{}; // UNSTABLE
    auto operator<=>(const SessionNewResult&) const = default;
};

struct SessionLoadParams {
    std::string sessionId;
    std::string cwd;
    std::vector<McpServer> mcpServers{};
    std::optional<std::vector<std::string>> additionalDirectories{};
    std::optional<SystemPrompt> systemPrompt{};
    auto operator<=>(const SessionLoadParams&) const = default;
};

struct SessionLoadResult {
    std::optional<SessionModeState> modes{};
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    std::optional<SessionModelState> models{}; // UNSTABLE
    auto operator<=>(const SessionLoadResult&) const = default;
};

struct SessionResumeParams {
    std::string sessionId;
    std::string cwd;
    std::optional<std::vector<McpServer>> mcpServers{};
    std::optional<std::vector<std::string>> additionalDirectories{};
    std::optional<SystemPrompt> systemPrompt{};
    auto operator<=>(const SessionResumeParams&) const = default;
};

struct SessionResumeResult {
    std::optional<SessionModeState> modes{};
    std::optional<std::vector<SessionConfigOption>> configOptions{};
    std::optional<SessionModelState> models{}; // UNSTABLE
    auto operator<=>(const SessionResumeResult&) const = default;
};

struct SessionListParams {
    std::optional<std::string> cwd{};
    std::optional<std::string> cursor{};
    auto operator<=>(const SessionListParams&) const = default;
};

struct SessionInfo {
    std::string sessionId;
    std::string cwd;
    std::optional<std::vector<std::string>> additionalDirectories{};
    std::optional<std::string> title{};
    std::optional<std::string> updatedAt{}; // ISO 8601
    auto operator<=>(const SessionInfo&) const = default;
};

struct SessionListResult {
    std::vector<SessionInfo> sessions{};
    std::optional<std::string> nextCursor{};
    auto operator<=>(const SessionListResult&) const = default;
};

struct SessionDeleteParams {
    std::string sessionId;
    auto operator<=>(const SessionDeleteParams&) const = default;
};

struct SessionDeleteResult {
    auto operator<=>(const SessionDeleteResult&) const = default;
};

struct SessionCloseParams {
    std::string sessionId;
    auto operator<=>(const SessionCloseParams&) const = default;
};

struct SessionCloseResult {
    auto operator<=>(const SessionCloseResult&) const = default;
};

struct SessionSetModeParams {
    std::string sessionId;
    std::string modeId;
    auto operator<=>(const SessionSetModeParams&) const = default;
};

struct SessionSetModeResult {
    auto operator<=>(const SessionSetModeResult&) const = default;
};

struct SessionSetConfigOptionParams {
    std::string sessionId;
    std::string configId;
    std::optional<std::string> type{}; // "boolean" when `value` is a bool
    SessionConfigValue value{std::string{}};
    auto operator<=>(const SessionSetConfigOptionParams&) const = default;
};

struct SessionSetConfigOptionResult {
    std::vector<SessionConfigOption> configOptions{};
    auto operator<=>(const SessionSetConfigOptionResult&) const = default;
};

// UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
struct SessionSetModelParams {
    std::string sessionId;
    std::string modelId;
    auto operator<=>(const SessionSetModelParams&) const = default;
};

// UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
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

// `$/cancel_request` (either direction): cancel an in-flight JSON-RPC request
struct CancelRequestParams {
    RequestId requestId{std::int64_t{0}};
    auto operator<=>(const CancelRequestParams&) const = default;
};

// --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
struct UserMessageChunk {
    std::string_view sessionUpdate{"user_message_chunk"};
    ContentBlock content;
    std::optional<std::string> messageId{};
    auto operator<=>(const UserMessageChunk&) const = default;
};

struct AgentMessageChunk {
    std::string_view sessionUpdate{"agent_message_chunk"};
    ContentBlock content;
    std::optional<std::string> messageId{};
    auto operator<=>(const AgentMessageChunk&) const = default;
};

struct AgentThoughtChunk {
    std::string_view sessionUpdate{"agent_thought_chunk"};
    ContentBlock content;
    std::optional<std::string> messageId{};
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

// `sessionUpdate: "tool_call"` (the spec's `ToolCall`): announces a new tool call
struct ToolCallUpdate {
    std::string_view sessionUpdate{"tool_call"};
    std::string toolCallId;
    std::string title;
    std::optional<std::string> name{};
    std::optional<std::string> kind{};   // "read", "edit", "delete", "move", "search", "execute", "think", "fetch", "switch_mode", "other"
    std::optional<std::string> status{}; // "pending", "in_progress", "completed", "failed"
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
    std::optional<RawJson> rawInput{};
    std::optional<RawJson> rawOutput{};
    auto operator<=>(const ToolCallUpdate&) const = default;
};

using ToolCall = ToolCallUpdate;

// `sessionUpdate: "tool_call_update"` (the spec's `ToolCallUpdate`): patches an existing tool call
struct ToolCallStatusUpdate {
    std::string_view sessionUpdate{"tool_call_update"};
    std::string toolCallId;
    std::optional<std::string> title{};
    std::optional<std::string> name{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
    std::optional<RawJson> rawInput{};
    std::optional<RawJson> rawOutput{};
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

// Unstructured command input: free text shown as a hint when the user types the command
struct AvailableCommandInput {
    std::string hint;
    auto operator<=>(const AvailableCommandInput&) const = default;
};

struct AvailableCommand {
    std::string name;
    std::string description;
    std::optional<AvailableCommandInput> input{};
    auto operator<=>(const AvailableCommand&) const = default;
};

struct AvailableCommandsUpdate {
    std::string_view sessionUpdate{"available_commands_update"};
    std::vector<AvailableCommand> availableCommands{};
    auto operator<=>(const AvailableCommandsUpdate&) const = default;
};

struct CurrentModeUpdate {
    std::string_view sessionUpdate{"current_mode_update"};
    std::string currentModeId;
    auto operator<=>(const CurrentModeUpdate&) const = default;
};

struct ConfigOptionUpdate {
    std::string_view sessionUpdate{"config_option_update"};
    std::vector<SessionConfigOption> configOptions{};
    auto operator<=>(const ConfigOptionUpdate&) const = default;
};

struct SessionInfoUpdate {
    std::string_view sessionUpdate{"session_info_update"};
    std::optional<std::string> title{};
    std::optional<std::string> updatedAt{};
    auto operator<=>(const SessionInfoUpdate&) const = default;
};

struct Cost {
    double amount{0.0};
    std::string currency; // ISO 4217, e.g. "USD"
    auto operator<=>(const Cost&) const = default;
};

struct UsageUpdate {
    std::string_view sessionUpdate{"usage_update"};
    std::uint64_t used{0}; // tokens currently in context
    std::uint64_t size{0}; // context window size in tokens
    std::optional<Cost> cost{};
    auto operator<=>(const UsageUpdate&) const = default;
};

struct NoticeUpdate {
    std::string_view sessionUpdate{"notice"};
    std::string severity{"info"}; // "info", "warning", "error"
    std::string title;
    std::optional<std::string> description{};
    auto operator<=>(const NoticeUpdate&) const = default;
};

struct CompactionUpdate {
    std::string_view sessionUpdate{"compaction_update"};
    std::string compactionId;
    std::string status{"in_progress"}; // "in_progress", "completed", "failed", "cancelled"
    std::optional<std::vector<ContentBlock>> summary{};
    std::optional<std::string> error{};
    auto operator<=>(const CompactionUpdate&) const = default;
};

struct CompactionSummaryChunk {
    std::string_view sessionUpdate{"compaction_summary_chunk"};
    std::string compactionId;
    ContentBlock content;
    auto operator<=>(const CompactionSummaryChunk&) const = default;
};

using SessionUpdatePayload = std::variant<
    UserMessageChunk,
    AgentMessageChunk,
    AgentThoughtChunk,
    ToolCallUpdate,
    ToolCallStatusUpdate,
    PlanUpdate,
    AvailableCommandsUpdate,
    CurrentModeUpdate,
    ConfigOptionUpdate,
    SessionInfoUpdate,
    UsageUpdate,
    NoticeUpdate,
    CompactionUpdate,
    CompactionSummaryChunk
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
    std::optional<std::string> name{};
    std::optional<std::string> kind{};
    std::optional<std::string> status{};
    std::optional<std::vector<ToolCallContent>> content{};
    std::optional<std::vector<ToolCallLocation>> locations{};
    std::optional<RawJson> rawInput{};
    std::optional<RawJson> rawOutput{};
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

// --- Elicitation: elicitation/create (Agent -> Client request), elicitation/complete (notification) ---
// Flattened form of the spec's mode/scope unions so unknown modes are preserved verbatim:
//   mode "form": `requestedSchema` (restricted JSON Schema) is set
//   mode "url":  `elicitationId` and `url` are set
//   scope: `sessionId` (+ optional `toolCallId`) or `requestId`
struct CreateElicitationParams {
    std::string mode{"form"};
    std::string message;
    std::optional<RawJson> requestedSchema{};
    std::optional<std::string> elicitationId{};
    std::optional<std::string> url{};
    std::optional<std::string> sessionId{};
    std::optional<std::string> toolCallId{};
    std::optional<RequestId> requestId{};
    auto operator<=>(const CreateElicitationParams&) const = default;
};

struct CreateElicitationResult {
    std::string action{"cancel"}; // "accept", "decline", "cancel"
    std::optional<RawJson> content{}; // submitted form values when action == "accept"
    auto operator<=>(const CreateElicitationResult&) const = default;
};

struct CompleteElicitationParams {
    std::string elicitationId;
    auto operator<=>(const CompleteElicitationParams&) const = default;
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

// --- Tagged-union discriminators (shared by all codecs) ---
// `untagged` names the alternative selected when the discriminator key is absent (-1: required).
template <typename Variant>
struct variant_tag;

template <>
struct variant_tag<ContentBlock> {
    static constexpr std::string_view key = "type";
    static constexpr std::array<std::string_view, 5> ids{"text", "image", "audio", "resource_link", "resource"};
    static constexpr int untagged = -1;
};

template <>
struct variant_tag<ToolCallContent> {
    static constexpr std::string_view key = "type";
    static constexpr std::array<std::string_view, 3> ids{"content", "diff", "terminal"};
    static constexpr int untagged = -1;
};

template <>
struct variant_tag<SessionUpdatePayload> {
    static constexpr std::string_view key = "sessionUpdate";
    static constexpr std::array<std::string_view, 14> ids{
        "user_message_chunk", "agent_message_chunk", "agent_thought_chunk", "tool_call",
        "tool_call_update", "plan", "available_commands_update", "current_mode_update",
        "config_option_update", "session_info_update", "usage_update", "notice",
        "compaction_update", "compaction_summary_chunk"};
    static constexpr int untagged = -1;
};

template <>
struct variant_tag<PermissionOutcome> {
    static constexpr std::string_view key = "outcome";
    static constexpr std::array<std::string_view, 2> ids{"cancelled", "selected"};
    static constexpr int untagged = -1;
};

template <>
struct variant_tag<McpServer> {
    static constexpr std::string_view key = "type";
    static constexpr std::array<std::string_view, 3> ids{"http", "sse", "stdio"};
    static constexpr int untagged = 2;
};

template <>
struct variant_tag<AuthMethod> {
    static constexpr std::string_view key = "type";
    static constexpr std::array<std::string_view, 2> ids{"agent", "terminal"};
    static constexpr int untagged = 0;
};

// --- Semantic validation of inbound params (beyond JSON shape) ---
// Returns a human-readable reason when `params` violates an ACP v1 MUST; both base classes reply
// with -32602 (Invalid params) in that case.
inline bool is_absolute_path(std::string_view path) noexcept {
    if (path.empty()) return false;
    if (path.front() == '/' || path.front() == '\\') return true;
    const auto drive = path.front();
    return path.size() >= 3 && ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) &&
           path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

namespace detail {
inline std::optional<std::string> require_id(std::string_view value, std::string_view field) {
    if (value.empty()) return std::string(field) + " is required";
    return std::nullopt;
}

inline std::optional<std::string> require_abs(std::string_view value, std::string_view field) {
    if (!is_absolute_path(value)) return std::string(field) + " must be an absolute path";
    return std::nullopt;
}

inline std::optional<std::string> require_abs(const std::optional<std::vector<std::string>>& values,
                                              std::string_view field) {
    if (values) {
        for (const auto& v : *values) {
            if (auto err = require_abs(v, field)) return err;
        }
    }
    return std::nullopt;
}

template <typename... Checks>
std::optional<std::string> first_error(Checks&&... checks) {
    std::optional<std::string> out{};
    ((out ? void() : void(out = std::forward<Checks>(checks))), ...);
    return out;
}
} // namespace detail

template <typename T>
std::optional<std::string> validate(const T&) {
    return std::nullopt;
}

inline std::optional<std::string> validate(const AuthenticateParams& p) {
    return detail::require_id(p.methodId, "methodId");
}
inline std::optional<std::string> validate(const SessionNewParams& p) {
    return detail::first_error(detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
inline std::optional<std::string> validate(const SessionLoadParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
inline std::optional<std::string> validate(const SessionResumeParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                               detail::require_abs(p.additionalDirectories, "additionalDirectories"));
}
inline std::optional<std::string> validate(const SessionListParams& p) {
    if (p.cwd) return detail::require_abs(*p.cwd, "cwd");
    return std::nullopt;
}
inline std::optional<std::string> validate(const SessionDeleteParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
inline std::optional<std::string> validate(const SessionCloseParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
inline std::optional<std::string> validate(const SessionSetModeParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_id(p.modeId, "modeId"));
}
inline std::optional<std::string> validate(const SessionSetConfigOptionParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.configId, "configId"));
}
inline std::optional<std::string> validate(const SessionSetModelParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.modelId, "modelId"));
}
inline std::optional<std::string> validate(const SessionPromptParams& p) {
    return detail::require_id(p.sessionId, "sessionId");
}
inline std::optional<std::string> validate(const RequestPermissionParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.toolCall.toolCallId, "toolCall.toolCallId"));
}
inline std::optional<std::string> validate(const CreateElicitationParams& p) {
    if (p.mode == "url") {
        return detail::first_error(detail::require_id(p.elicitationId.value_or(""), "elicitationId"),
                                   detail::require_id(p.url.value_or(""), "url"));
    }
    return std::nullopt;
}
inline std::optional<std::string> validate(const FsReadTextFileParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
}
inline std::optional<std::string> validate(const FsWriteTextFileParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
}
inline std::optional<std::string> validate(const TerminalCreateParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.command, "command"));
}
inline std::optional<std::string> validate(const TerminalOutputParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
inline std::optional<std::string> validate(const TerminalWaitForExitParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
inline std::optional<std::string> validate(const TerminalKillParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}
inline std::optional<std::string> validate(const TerminalReleaseParams& p) {
    return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                               detail::require_id(p.terminalId, "terminalId"));
}

} // namespace buraq::v1
