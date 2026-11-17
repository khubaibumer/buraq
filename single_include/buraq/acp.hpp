// ============================================================================
// buraq v0.1.0 - Agent Client Protocol (ACP) v1 SDK for C++20/23
// https://github.com/khubaibumer/buraq
//
// Single-header distribution: buraq/acp.hpp
// Zero third-party dependencies; codecs ship as separate headers.
//
// GENERATED FILE - DO NOT EDIT. Regenerate with: python3 scripts/amalgamate.py
// SPDX-License-Identifier: Apache-2.0
// ============================================================================
#pragma once

// ---- begin buraq/detail/compat.hpp ----
#include <version>
#include <utility>
#include <variant>
#include <functional>

// 1. expected<T, E>
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202211L
#include <expected>
namespace buraq {
using std::expected;
using std::unexpected;
}
#else
namespace buraq {
template <typename E>
struct unexpected {
  E error;
  explicit constexpr unexpected(E e) : error(std::move(e)) {}
};

template <typename T, typename E>
class expected {
  std::variant<T, unexpected<E>> storage_;
public:
  constexpr expected(T val) : storage_(std::move(val)) {}
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr T& value() & { return std::get<T>(storage_); }
  constexpr const T& value() const& { return std::get<T>(storage_); }
  constexpr T&& value() && { return std::get<T>(std::move(storage_)); }

  constexpr T& operator*() & { return value(); }
  constexpr const T& operator*() const& { return value(); }
  constexpr T&& operator*() && { return std::move(value()); }

  constexpr T* operator->() { return &value(); }
  constexpr const T* operator->() const { return &value(); }

  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  template <typename F>
  constexpr auto and_then(F&& f) & {
    using Ret = std::invoke_result_t<F, T&>;
    if (has_value()) return std::invoke(std::forward<F>(f), value());
    return Ret(unexpected<E>(error()));
  }
};

template <typename E>
class expected<void, E> {
  std::variant<std::monostate, unexpected<E>> storage_;
public:
  constexpr expected() noexcept : storage_(std::monostate{}) {}
  constexpr expected(unexpected<E> err) : storage_(std::move(err)) {}

  [[nodiscard]] constexpr bool has_value() const noexcept {
    return std::holds_alternative<std::monostate>(storage_);
  }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  constexpr void value() const noexcept {}
  constexpr void operator*() const noexcept {}

  constexpr E& error() & { return std::get<unexpected<E>>(storage_).error; }
  constexpr const E& error() const& { return std::get<unexpected<E>>(storage_).error; }

  template <typename F>
  constexpr auto and_then(F&& f) & {
    using Ret = std::invoke_result_t<F>;
    if (has_value()) return std::invoke(std::forward<F>(f));
    return Ret(unexpected<E>(error()));
  }
};
}
#endif

// 2. generator<T> opt-in sugar (GCC 14+ / MSVC with -std=c++23)
#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
#include <generator>
#define BURAQ_HAS_STD_GENERATOR 1
namespace buraq {
using std::generator;
}
#else
#define BURAQ_HAS_STD_GENERATOR 0
#endif
// ---- end buraq/detail/compat.hpp ----

// ---- begin buraq/protocol/jsonrpc.hpp ----
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace buraq {

using RequestId = std::variant<std::int64_t, std::string>;

// Arbitrary, codec-neutral JSON value kept as serialized text (e.g. `rawInput`, elicitation schemas,
// extension-method payloads). An empty string encodes as JSON `null`.
struct RawJson {
    std::string json{};
    auto operator<=>(const RawJson&) const = default;
};

namespace detail {

// Serialize `text` as a JSON string literal (RFC 8259 escaping)
inline std::string json_quote(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += buf;
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
    return out;
}

} // namespace detail

// JSON-RPC 2.0 and ACP v1 error codes
namespace error_code {
inline constexpr int parse_error = -32700;
inline constexpr int invalid_request = -32600;
inline constexpr int method_not_found = -32601;
inline constexpr int invalid_params = -32602;
inline constexpr int internal_error = -32603;
inline constexpr int request_cancelled = -32800;
inline constexpr int auth_required = -32000;
inline constexpr int resource_not_found = -32002;
} // namespace error_code

struct RpcError {
    int code{-32603};
    std::string message;
    std::optional<std::string> data_json{};

    static RpcError parse_error(std::string_view msg = "Parse error") {
        return {-32700, std::string(msg), std::nullopt};
    }
    static RpcError invalid_request(std::string_view msg = "Invalid Request") {
        return {-32600, std::string(msg), std::nullopt};
    }
    static RpcError method_not_found(std::string_view method) {
        return {-32601, "Method not found: " + std::string(method), std::nullopt};
    }
    static RpcError invalid_params(std::string_view msg = "Invalid params") {
        return {-32602, std::string(msg), std::nullopt};
    }
    static RpcError internal_error(std::string_view msg = "Internal error") {
        return {-32603, std::string(msg), std::nullopt};
    }
    static RpcError request_cancelled() {
        return {-32800, "Request cancelled", std::nullopt};
    }
    static RpcError auth_required(std::string_view msg = "Authentication required") {
        return {-32000, std::string(msg), std::nullopt};
    }
    static RpcError resource_not_found(std::string_view msg = "Resource not found",
                                       std::optional<std::string> data_json = std::nullopt) {
        return {-32002, std::string(msg), std::move(data_json)};
    }
    // -32002 with `data.sessionId`, as recommended for unknown or closed sessions
    static RpcError session_not_found(std::string_view session_id) {
        return resource_not_found("Session not found: " + std::string(session_id),
                                  "{\"sessionId\":" + detail::json_quote(session_id) + "}");
    }
};

// Raw view over a decoded JSON-RPC envelope (params/result kept as raw JSON slices)
struct RawEnvelope {
    std::optional<RequestId> id{};
    std::string_view method{};
    std::string_view params_json{};
    std::string_view result_json{};
    std::optional<RpcError> error{};

    [[nodiscard]] bool is_request() const noexcept {
        return id.has_value() && !method.empty();
    }
    [[nodiscard]] bool is_notification() const noexcept {
        return !id.has_value() && !method.empty();
    }
    [[nodiscard]] bool is_response() const noexcept {
        return id.has_value() && method.empty();
    }
};

// Pluggable zero-overhead JSON codec contract
template <typename Codec>
concept JsonCodec = requires(std::span<const std::byte> frame,
                             std::string_view raw_json,
                             const RequestId& id,
                             const RpcError& err) {
    // Parse top-level JSON-RPC 2.0 envelope without eagerly decoding payload
    { Codec::parse_envelope(frame) } -> std::same_as<expected<RawEnvelope, RpcError>>;

    // Encode a JSON-RPC 2.0 error response
    { Codec::encode_error(id, err) } -> std::same_as<std::string>;
};

// Typed payload encode/decode requirements checked per message type
template <typename Codec, typename T>
concept JsonSerializable = JsonCodec<Codec> && requires(std::string_view json,
                                                        const RequestId& id,
                                                        std::string_view method,
                                                        const T& val) {
    { Codec::template decode<T>(json) } -> std::same_as<expected<T, RpcError>>;
    { Codec::template encode_result<T>(id, val) } -> std::same_as<std::string>;
    { Codec::template encode_request<T>(id, method, val) } -> std::same_as<std::string>;
    { Codec::template encode_notification<T>(method, val) } -> std::same_as<std::string>;
};

} // namespace buraq
// ---- end buraq/protocol/jsonrpc.hpp ----

// ---- begin buraq/protocol/schema_v1.hpp ----
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
// ---- end buraq/protocol/schema_v1.hpp ----

// ---- begin buraq/core/agent_base.hpp ----
#include <concepts>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace buraq {

// CRTP base for ACP agents. Every handler is optional and detected at compile time; requests for
// methods the derived class does not implement are answered with -32601 (Method not found).
//
// Client -> Agent requests:   on_initialize, on_authenticate, on_logout, on_session_new, on_session_load,
//                             on_session_resume, on_session_list, on_session_delete, on_session_close,
//                             on_session_set_mode, on_session_set_config_option, on_session_set_model (unstable),
//                             on_session_prompt / on_session_prompt_stream, on_ext_method
// Client -> Agent notifications: on_session_cancel, on_cancel_request, on_ext_notification
// Responses to agent-initiated requests: on_raw_response
template <typename Derived, JsonCodec Codec>
class AgentBase {
protected:
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

public:
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) {
            return unexpected(env.error());
        }
        return dispatch_message(*env, std::forward<OutboundSink>(sink));
    }

    // True once `initialize` has completed successfully on this connection
    [[nodiscard]] bool initialized() const noexcept { return initialized_; }

    // --- Negotiated Client Capabilities (populated by `initialize`) ---
    [[nodiscard]] const std::optional<v1::ClientCapabilities>& client_capabilities() const noexcept {
        return client_capabilities_;
    }

    [[nodiscard]] const std::optional<v1::ImplementationInfo>& client_info() const noexcept {
        return client_info_;
    }

    [[nodiscard]] bool client_can_read_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->readTextFile;
    }

    [[nodiscard]] bool client_can_write_text_file() const noexcept {
        return client_capabilities_ && client_capabilities_->fs && client_capabilities_->fs->writeTextFile;
    }

    [[nodiscard]] bool client_supports_terminal() const noexcept {
        return client_capabilities_ && client_capabilities_->terminal;
    }

    [[nodiscard]] bool client_supports_elicitation_form() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->form;
    }

    [[nodiscard]] bool client_supports_elicitation_url() const noexcept {
        return client_capabilities_ && client_capabilities_->elicitation && client_capabilities_->elicitation->url;
    }

    [[nodiscard]] bool client_supports_terminal_auth() const noexcept {
        return client_capabilities_ && client_capabilities_->auth && client_capabilities_->auth->terminal;
    }

    [[nodiscard]] bool client_supports_boolean_config_options() const noexcept {
        return client_capabilities_ && client_capabilities_->session &&
               client_capabilities_->session->configOptions && client_capabilities_->session->configOptions->boolean;
    }

    // --- Notifications (Agent -> Client) ---
    template <typename OutboundSink>
    void emit_session_update(const v1::SessionUpdateNotification& update, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/update", update), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void notify_elicitation_complete(const v1::CompleteElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("elicitation/complete", params), std::forward<OutboundSink>(sink));
    }

    // Ask the client to abandon a request this agent sent earlier (`$/cancel_request`)
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension notification
    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Delegated Client Requests (Agent -> Client); replies arrive via on_raw_response ---
    template <typename OutboundSink>
    void request_permission(const RequestId& id, const v1::RequestPermissionParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "session/request_permission", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_fs_read(const RequestId& id, const v1::FsReadTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/read_text_file", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_fs_write(const RequestId& id, const v1::FsWriteTextFileParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "fs/write_text_file", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_create(const RequestId& id, const v1::TerminalCreateParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/create", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_output(const RequestId& id, const v1::TerminalOutputParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/output", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_wait_for_exit(const RequestId& id,
                                        const v1::TerminalWaitForExitParams& params,
                                        OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/wait_for_exit", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_kill(const RequestId& id, const v1::TerminalKillParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/kill", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_terminal_release(const RequestId& id, const v1::TerminalReleaseParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "terminal/release", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void request_elicitation(const RequestId& id, const v1::CreateElicitationParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, "elicitation/create", params), std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    template <typename OutboundSink>
    void request_ext_method(const RequestId& id, std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_request(id, method, params), std::forward<OutboundSink>(sink));
    }

private:
    bool initialized_{false};
    std::optional<v1::ClientCapabilities> client_capabilities_{};
    std::optional<v1::ImplementationInfo> client_info_{};

    template <typename OutboundSink>
    expected<void, RpcError> dispatch_message(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.is_response()) {
            if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
                self().on_raw_response(env);
            }
            return {};
        }

        if (env.method == "initialize") {
            if constexpr (requires(Derived& d, const v1::InitializeParams& p) { d.on_initialize(p); }) {
                return handle_request<v1::InitializeParams>(
                    env, sink, [this](Derived& d, const v1::InitializeParams& p, OutboundSink&) {
                        client_capabilities_ = p.clientCapabilities;
                        client_info_ = p.clientInfo;
                        auto result = d.on_initialize(p);
                        initialized_ = result.has_value();
                        if (!result) {
                            client_capabilities_.reset();
                            client_info_.reset();
                        }
                        return result;
                    });
            }
        }

        // Agents that implement `initialize` reject every other request until it succeeds
        if constexpr (requires(Derived& d, const v1::InitializeParams& p) { d.on_initialize(p); }) {
            if (!initialized_ && env.method != "initialize") {
                if (env.id.has_value()) {
                    emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_request(
                                                                "Connection not initialized: send `initialize` first")),
                               sink);
                }
                return {};
            }
        }

        if (env.method == "authenticate") {
            if constexpr (requires(Derived& d, const v1::AuthenticateParams& p) { d.on_authenticate(p); }) {
                return handle_request<v1::AuthenticateParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_authenticate(p); });
            }
        }

        if (env.method == "logout") {
            if constexpr (requires(Derived& d, const v1::LogoutParams& p) { d.on_logout(p); }) {
                return handle_request<v1::LogoutParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_logout(p); });
            }
        }

        if (env.method == "session/new") {
            if constexpr (requires(Derived& d, const v1::SessionNewParams& p) { d.on_session_new(p); }) {
                return handle_request<v1::SessionNewParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_new(p); });
            }
        }

        if (env.method == "session/load") {
            if constexpr (requires(Derived& d, const v1::SessionLoadParams& p) { d.on_session_load(p); }) {
                return handle_request<v1::SessionLoadParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_load(p); });
            }
        }

        if (env.method == "session/resume") {
            if constexpr (requires(Derived& d, const v1::SessionResumeParams& p) { d.on_session_resume(p); }) {
                return handle_request<v1::SessionResumeParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_resume(p); });
            }
        }

        if (env.method == "session/list") {
            if constexpr (requires(Derived& d, const v1::SessionListParams& p) { d.on_session_list(p); }) {
                return handle_request<v1::SessionListParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_list(p); });
            }
        }

        if (env.method == "session/delete") {
            if constexpr (requires(Derived& d, const v1::SessionDeleteParams& p) { d.on_session_delete(p); }) {
                return handle_request<v1::SessionDeleteParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_delete(p); });
            }
        }

        if (env.method == "session/close") {
            if constexpr (requires(Derived& d, const v1::SessionCloseParams& p) { d.on_session_close(p); }) {
                return handle_request<v1::SessionCloseParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_close(p); });
            }
        }

        if (env.method == "session/set_mode") {
            if constexpr (requires(Derived& d, const v1::SessionSetModeParams& p) { d.on_session_set_mode(p); }) {
                return handle_request<v1::SessionSetModeParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_mode(p); });
            }
        }

        if (env.method == "session/set_config_option") {
            if constexpr (requires(Derived& d, const v1::SessionSetConfigOptionParams& p) {
                d.on_session_set_config_option(p);
            }) {
                return handle_request<v1::SessionSetConfigOptionParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_config_option(p); });
            }
        }

        if (env.method == "session/set_model") {
            if constexpr (requires(Derived& d, const v1::SessionSetModelParams& p) { d.on_session_set_model(p); }) {
                return handle_request<v1::SessionSetModelParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_set_model(p); });
            }
        }

        if (env.method == "session/prompt") {
#if BURAQ_HAS_STD_GENERATOR
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p) {
                { d.on_session_prompt_stream(p) } -> std::same_as<generator<v1::SessionUpdatePayload>>;
            }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, OutboundSink& s) {
                        for (auto&& update : d.on_session_prompt_stream(p)) {
                            d.emit_session_update(v1::SessionUpdateNotification{
                                .sessionId = p.sessionId,
                                .update = std::forward<decltype(update)>(update),
                            }, s);
                        }
                        return expected<v1::SessionPromptResult, RpcError>(
                            v1::SessionPromptResult{.stopReason = "end_turn"});
                    });
            }
#endif
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p, OutboundSink& s) {
                d.on_session_prompt(p, s);
            }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, OutboundSink& s) { return d.on_session_prompt(p, s); });
            }
            if constexpr (requires(Derived& d, const v1::SessionPromptParams& p) { d.on_session_prompt(p); }) {
                return handle_request<v1::SessionPromptParams>(
                    env, sink, [](Derived& d, const auto& p, auto&) { return d.on_session_prompt(p); });
            }
        }

        if (env.method == "session/cancel") {
            if constexpr (requires(Derived& d, const v1::SessionCancelParams& p) { d.on_session_cancel(p); }) {
                if (auto params = Codec::template decode<v1::SessionCancelParams>(env.params_json)) {
                    self().on_session_cancel(*params);
                }
            }
            return {};
        }

        if (env.method == "$/cancel_request") {
            if constexpr (requires(Derived& d, const v1::CancelRequestParams& p) { d.on_cancel_request(p); }) {
                if (auto params = Codec::template decode<v1::CancelRequestParams>(env.params_json)) {
                    self().on_cancel_request(*params);
                }
            }
            return {};
        }

        if (!env.method.empty() && env.method.front() == '_') {
            return dispatch_extension(env, sink);
        }

        if (env.id.has_value()) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
        }
        return {};
    }

    template <typename OutboundSink>
    expected<void, RpcError> dispatch_extension(const RawEnvelope& env, OutboundSink& sink) {
        const RawJson params{std::string(env.params_json)};
        if (env.id.has_value()) {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError>>;
            }) {
                auto result = self().on_ext_method(env.method, params);
                if (!result) {
                    emit_bytes(Codec::encode_error(*env.id, result.error()), sink);
                } else {
                    emit_bytes(Codec::encode_result(*env.id, *result), sink);
                }
                return {};
            }
            emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
            return {};
        }
        if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) { d.on_ext_notification(m, p); }) {
            self().on_ext_notification(env.method, params);
        }
        return {};
    }

    template <typename Params, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_request(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
        if (!env.id.has_value()) {
            return unexpected(RpcError::invalid_request("Expected request id for " + std::string(env.method)));
        }

        auto params = Codec::template decode<Params>(env.params_json);
        if (!params) {
            emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
            return {};
        }
        if (auto reason = v1::validate(*params)) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
            return {};
        }

        auto result = handler(self(), *params, sink);
        if (!result) {
            emit_bytes(Codec::encode_error(*env.id, result.error()), sink);
        } else {
            emit_bytes(Codec::encode_result(*env.id, *result), sink);
        }
        return {};
    }
};

} // namespace buraq
// ---- end buraq/core/agent_base.hpp ----

// ---- begin buraq/core/client_base.hpp ----
#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace buraq {

// CRTP base for ACP clients. Every handler is optional and detected at compile time; agent requests for
// methods the derived class does not implement are answered with -32601 (Method not found).
//
// Agent -> Client requests:      on_request_permission, on_fs_read_text_file, on_fs_write_text_file,
//                                on_terminal_{create,output,wait_for_exit,kill,release}, on_elicitation_create,
//                                on_ext_method
// Agent -> Client notifications: on_session_update (+ on_unhandled_session_update), on_elicitation_complete,
//                                on_cancel_request, on_ext_notification
// Responses without a typed callback: on_raw_response
template <typename Derived, JsonCodec Codec>
class ClientBase {
protected:
    constexpr Derived& self() noexcept { return static_cast<Derived&>(*this); }
    constexpr const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    using ResponseHandler = std::function<void(const RawEnvelope&)>;

    std::int64_t next_id_{1};
    std::unordered_map<std::int64_t, ResponseHandler> pending_requests_{};

public:
    // --- Fire-and-ID Outbound Requests (for custom correlation via on_raw_response) ---
    template <typename OutboundSink>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink) {
        return send_request_raw("initialize", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink) {
        return send_request_raw("authenticate", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink) {
        return send_request_raw("logout", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink) {
        return send_request_raw("session/new", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink) {
        return send_request_raw("session/load", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/resume", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink) {
        return send_request_raw("session/list", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink) {
        return send_request_raw("session/delete", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink) {
        return send_request_raw("session/close", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_mode", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_config_option", params, std::forward<OutboundSink>(sink));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    template <typename OutboundSink>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink) {
        return send_request_raw("session/set_model", params, std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink) {
        return send_request_raw("session/prompt", params, std::forward<OutboundSink>(sink));
    }

    // Custom `_`-prefixed extension request
    template <typename OutboundSink>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        return send_request_raw(method, params, std::forward<OutboundSink>(sink));
    }

    // --- Typed Callback Outbound Requests (automatic decoding & ID correlation) ---
    // `initialize` responses are also recorded for agent_capabilities() / agent_info() / auth_methods()
    template <typename OutboundSink, typename Callback>
    std::int64_t send_initialize(const v1::InitializeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::InitializeResult>(
            "initialize", params, std::forward<OutboundSink>(sink),
            [this, callback = std::forward<Callback>(cb)](expected<v1::InitializeResult, RpcError> res) mutable {
                if (res) {
                    agent_capabilities_ = res->agentCapabilities;
                    agent_info_ = res->agentInfo;
                    auth_methods_ = res->authMethods;
                }
                callback(std::move(res));
            });
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_authenticate(const v1::AuthenticateParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::AuthenticateResult>(
            "authenticate", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_logout(const v1::LogoutParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::LogoutResult>(
            "logout", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_new(const v1::SessionNewParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionNewResult>(
            "session/new", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_load(const v1::SessionLoadParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionLoadResult>(
            "session/load", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_resume(const v1::SessionResumeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionResumeResult>(
            "session/resume", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_list(const v1::SessionListParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionListResult>(
            "session/list", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_delete(const v1::SessionDeleteParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionDeleteResult>(
            "session/delete", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_close(const v1::SessionCloseParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionCloseResult>(
            "session/close", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_mode(const v1::SessionSetModeParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModeResult>(
            "session/set_mode", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_config_option(const v1::SessionSetConfigOptionParams& params,
                                                OutboundSink&& sink,
                                                Callback&& cb) {
        return send_request_typed<v1::SessionSetConfigOptionResult>(
            "session/set_config_option", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_set_model(const v1::SessionSetModelParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionSetModelResult>(
            "session/set_model", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_session_prompt(const v1::SessionPromptParams& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<v1::SessionPromptResult>(
            "session/prompt", params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    template <typename OutboundSink, typename Callback>
    std::int64_t send_ext_method(std::string_view method, const RawJson& params, OutboundSink&& sink, Callback&& cb) {
        return send_request_typed<RawJson>(method, params, std::forward<OutboundSink>(sink), std::forward<Callback>(cb));
    }

    // --- Outbound Notifications ---
    template <typename OutboundSink>
    void send_session_cancel(const v1::SessionCancelParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("session/cancel", params), std::forward<OutboundSink>(sink));
    }

    // Ask the agent to abandon an in-flight request (`$/cancel_request`). The pending callback stays
    // registered: the agent still answers, typically with -32800 (Request cancelled).
    template <typename OutboundSink>
    void send_cancel_request(const v1::CancelRequestParams& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification("$/cancel_request", params), std::forward<OutboundSink>(sink));
    }

    template <typename OutboundSink>
    void send_ext_notification(std::string_view method, const RawJson& params, OutboundSink&& sink) {
        emit_bytes(Codec::encode_notification(method, params), std::forward<OutboundSink>(sink));
    }

    // --- Negotiated Agent Capabilities (populated by the typed send_initialize overload) ---
    [[nodiscard]] const std::optional<v1::AgentCapabilities>& agent_capabilities() const noexcept {
        return agent_capabilities_;
    }

    [[nodiscard]] const std::optional<v1::ImplementationInfo>& agent_info() const noexcept {
        return agent_info_;
    }

    [[nodiscard]] const std::vector<v1::AuthMethod>& auth_methods() const noexcept {
        return auth_methods_;
    }

    [[nodiscard]] std::size_t pending_count() const noexcept {
        return pending_requests_.size();
    }

    // Fail and clear all in-flight requests (useful on transport disconnect)
    void cancel_all_pending(const RpcError& reason = RpcError::request_cancelled()) {
        auto pending = std::move(pending_requests_);
        pending_requests_.clear();
        for (auto& [id, handler] : pending) {
            RawEnvelope err_env{.id = RequestId{id}, .error = reason};
            handler(err_env);
        }
    }

    // --- Inbound Frame Entry Point ---
    template <typename OutboundSink>
    expected<void, RpcError> feed(std::span<const std::byte> frame, OutboundSink&& sink) {
        auto env = Codec::parse_envelope(frame);
        if (!env) return unexpected(env.error());

        if (env->is_notification()) {
            return dispatch_notification(*env);
        }
        if (env->is_request()) {
            return dispatch_agent_request(*env, std::forward<OutboundSink>(sink));
        }
        if (env->is_response()) {
            return dispatch_response(*env);
        }
        return unexpected(RpcError::invalid_request());
    }

private:
    std::optional<v1::AgentCapabilities> agent_capabilities_{};
    std::optional<v1::ImplementationInfo> agent_info_{};
    std::vector<v1::AuthMethod> auth_methods_{};

    template <typename OutboundSink>
    static void emit_bytes(std::string_view wire, OutboundSink&& sink) {
        sink(std::as_bytes(std::span{wire.data(), wire.size()}));
    }

    template <typename Params, typename OutboundSink>
    std::int64_t send_request_raw(std::string_view method, const Params& params, OutboundSink&& sink) {
        const auto id = next_id_++;
        emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
        return id;
    }

    template <typename Result, typename Params, typename OutboundSink, typename Callback>
    std::int64_t send_request_typed(std::string_view method,
                                    const Params& params,
                                    OutboundSink&& sink,
                                    Callback&& cb) {
        const auto id = next_id_++;
        pending_requests_.emplace(id, [callback = std::forward<Callback>(cb)](const RawEnvelope& env) mutable {
            if (env.error.has_value()) {
                callback(expected<Result, RpcError>(unexpected(*env.error)));
                return;
            }
            callback(Codec::template decode<Result>(env.result_json));
        });

        emit_bytes(Codec::encode_request(RequestId{id}, method, params), std::forward<OutboundSink>(sink));
        return id;
    }

    expected<void, RpcError> dispatch_notification(const RawEnvelope& env) {
        if (env.method == "session/update") {
            if constexpr (requires(Derived& d, const v1::SessionUpdateNotification& u) {
                d.on_session_update(u);
            }) {
                auto update = Codec::template decode<v1::SessionUpdateNotification>(env.params_json);
                if (!update) {
                    // Forward-compat: newer agents may emit sessionUpdate kinds this SDK does not model yet
                    if constexpr (requires(Derived& d, const RawEnvelope& e, const RpcError& err) {
                        d.on_unhandled_session_update(e, err);
                    }) {
                        self().on_unhandled_session_update(env, update.error());
                        return {};
                    }
                    return unexpected(update.error());
                }
                self().on_session_update(*update);
            }
            return {};
        }

        if (env.method == "elicitation/complete") {
            if constexpr (requires(Derived& d, const v1::CompleteElicitationParams& p) {
                d.on_elicitation_complete(p);
            }) {
                auto params = Codec::template decode<v1::CompleteElicitationParams>(env.params_json);
                if (!params) return unexpected(params.error());
                self().on_elicitation_complete(*params);
            }
            return {};
        }

        if (env.method == "$/cancel_request") {
            if constexpr (requires(Derived& d, const v1::CancelRequestParams& p) { d.on_cancel_request(p); }) {
                auto params = Codec::template decode<v1::CancelRequestParams>(env.params_json);
                if (!params) return unexpected(params.error());
                self().on_cancel_request(*params);
            }
            return {};
        }

        if (!env.method.empty() && env.method.front() == '_') {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                d.on_ext_notification(m, p);
            }) {
                self().on_ext_notification(env.method, RawJson{std::string(env.params_json)});
            }
        }
        return {};
    }

    template <typename Params, typename OutboundSink, typename Handler>
    expected<void, RpcError> handle_agent_request(const RawEnvelope& env, OutboundSink& sink, Handler&& handler) {
        auto params = Codec::template decode<Params>(env.params_json);
        if (!params) {
            emit_bytes(Codec::encode_error(*env.id, params.error()), sink);
            return {};
        }
        if (auto reason = v1::validate(*params)) {
            emit_bytes(Codec::encode_error(*env.id, RpcError::invalid_params(*reason)), sink);
            return {};
        }

        auto res = handler(self(), *params);
        if (!res) {
            emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
        } else {
            emit_bytes(Codec::encode_result(*env.id, *res), sink);
        }
        return {};
    }

    template <typename OutboundSink>
    expected<void, RpcError> dispatch_agent_request(const RawEnvelope& env, OutboundSink&& sink) {
        if (env.method == "session/request_permission") {
            if constexpr (requires(Derived& d, const v1::RequestPermissionParams& p) { d.on_request_permission(p); }) {
                return handle_agent_request<v1::RequestPermissionParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_request_permission(p); });
            }
        }

        if (env.method == "fs/read_text_file") {
            if constexpr (requires(Derived& d, const v1::FsReadTextFileParams& p) { d.on_fs_read_text_file(p); }) {
                return handle_agent_request<v1::FsReadTextFileParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_fs_read_text_file(p); });
            }
        }

        if (env.method == "fs/write_text_file") {
            if constexpr (requires(Derived& d, const v1::FsWriteTextFileParams& p) { d.on_fs_write_text_file(p); }) {
                return handle_agent_request<v1::FsWriteTextFileParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_fs_write_text_file(p); });
            }
        }

        if (env.method == "terminal/create") {
            if constexpr (requires(Derived& d, const v1::TerminalCreateParams& p) { d.on_terminal_create(p); }) {
                return handle_agent_request<v1::TerminalCreateParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_create(p); });
            }
        }

        if (env.method == "terminal/output") {
            if constexpr (requires(Derived& d, const v1::TerminalOutputParams& p) { d.on_terminal_output(p); }) {
                return handle_agent_request<v1::TerminalOutputParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_output(p); });
            }
        }

        if (env.method == "terminal/wait_for_exit") {
            if constexpr (requires(Derived& d, const v1::TerminalWaitForExitParams& p) {
                d.on_terminal_wait_for_exit(p);
            }) {
                return handle_agent_request<v1::TerminalWaitForExitParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_wait_for_exit(p); });
            }
        }

        if (env.method == "terminal/kill") {
            if constexpr (requires(Derived& d, const v1::TerminalKillParams& p) { d.on_terminal_kill(p); }) {
                return handle_agent_request<v1::TerminalKillParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_kill(p); });
            }
        }

        if (env.method == "terminal/release") {
            if constexpr (requires(Derived& d, const v1::TerminalReleaseParams& p) { d.on_terminal_release(p); }) {
                return handle_agent_request<v1::TerminalReleaseParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_terminal_release(p); });
            }
        }

        if (env.method == "elicitation/create") {
            if constexpr (requires(Derived& d, const v1::CreateElicitationParams& p) { d.on_elicitation_create(p); }) {
                return handle_agent_request<v1::CreateElicitationParams>(
                    env, sink, [](Derived& d, const auto& p) { return d.on_elicitation_create(p); });
            }
        }

        if (!env.method.empty() && env.method.front() == '_') {
            if constexpr (requires(Derived& d, std::string_view m, const RawJson& p) {
                { d.on_ext_method(m, p) } -> std::same_as<expected<RawJson, RpcError>>;
            }) {
                auto res = self().on_ext_method(env.method, RawJson{std::string(env.params_json)});
                if (!res) {
                    emit_bytes(Codec::encode_error(*env.id, res.error()), sink);
                } else {
                    emit_bytes(Codec::encode_result(*env.id, *res), sink);
                }
                return {};
            }
        }

        emit_bytes(Codec::encode_error(*env.id, RpcError::method_not_found(env.method)), sink);
        return {};
    }

    expected<void, RpcError> dispatch_response(const RawEnvelope& env) {
        if (env.id && std::holds_alternative<std::int64_t>(*env.id)) {
            const auto id = std::get<std::int64_t>(*env.id);
            if (auto it = pending_requests_.find(id); it != pending_requests_.end()) {
                auto handler = std::move(it->second);
                pending_requests_.erase(it);
                handler(env);
                return {};
            }
        }

        if constexpr (requires(Derived& d, const RawEnvelope& r) { d.on_raw_response(r); }) {
            self().on_raw_response(env);
        }
        return {};
    }
};

} // namespace buraq
// ---- end buraq/core/client_base.hpp ----

// ---- begin buraq/acp.hpp ----
// include/buraq/acp.hpp
// ---- end buraq/acp.hpp ----

// ---- begin buraq/transport/stdio.hpp ----
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace buraq::transport {

// Sink callable compatible with AgentBase::feed(..., sink)
struct StdoutSink {
  std::ostream& out{std::cout};

  void operator()(std::span<const std::byte> bytes) const {
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (bytes.empty() || bytes.back() != std::byte{'\n'}) {
      out.put('\n');
    }
    out.flush();
  }
};

// Blocking line loop for standalone CLI ACP agents
template <typename Agent>
int run_stdio_agent(Agent& agent, std::istream& in = std::cin, std::ostream& out = std::cout) {
  // Untie cin/cout and disable stdio sync for fast pipe throughput
  std::ios::sync_with_stdio(false);
  in.tie(nullptr);

  StdoutSink sink{out};
  std::string line;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    auto bytes = std::as_bytes(std::span{line.data(), line.size()});
    auto status = agent.feed(bytes, sink);
    if (!status) {
      std::cerr << "[buraq] frame error: " << status.error().message << '\n';
    }
  }
  return 0;
}

} // namespace buraq::transport
// ---- end buraq/transport/stdio.hpp ----
