#pragma once

#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

namespace buraq {

namespace detail {

template <typename T>
void put_opt(nlohmann::json& j, const char* key, const std::optional<T>& opt) {
    if (opt.has_value()) {
        j[key] = *opt;
    }
}

template <typename T>
void get_opt(const nlohmann::json& j, const char* key, std::optional<T>& opt) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        opt = it->get<T>();
    } else {
        opt.reset();
    }
}

inline nlohmann::json id_to_json(const RequestId& id) {
    return std::visit([](const auto& v) -> nlohmann::json { return v; }, id);
}

} // namespace detail

namespace v1 {

// Forward declarations for recursive variants (ContentBlock <-> ToolCallContent)
inline void to_json(nlohmann::json& j, const ContentBlock& v);
inline void from_json(const nlohmann::json& j, ContentBlock& v);

// --- Capabilities & Info ---
inline void to_json(nlohmann::json& j, const ImplementationInfo& v) {
    j = nlohmann::json{{"name", v.name}, {"version", v.version}};
    detail::put_opt(j, "title", v.title);
}
inline void from_json(const nlohmann::json& j, ImplementationInfo& v) {
    j.at("name").get_to(v.name);
    j.at("version").get_to(v.version);
    detail::get_opt(j, "title", v.title);
}

inline void to_json(nlohmann::json& j, const ClientCapabilities::FsCapabilities& v) {
    j = nlohmann::json{{"readTextFile", v.readTextFile}, {"writeTextFile", v.writeTextFile}};
}
inline void from_json(const nlohmann::json& j, ClientCapabilities::FsCapabilities& v) {
    v.readTextFile = j.value("readTextFile", false);
    v.writeTextFile = j.value("writeTextFile", false);
}

inline void to_json(nlohmann::json& j, const ClientCapabilities& v) {
    j = nlohmann::json{{"terminal", v.terminal}};
    detail::put_opt(j, "fs", v.fs);
}
inline void from_json(const nlohmann::json& j, ClientCapabilities& v) {
    v.terminal = j.value("terminal", false);
    detail::get_opt(j, "fs", v.fs);
}

inline void to_json(nlohmann::json& j, const AgentCapabilities::PromptCapabilities& v) {
    j = nlohmann::json{{"image", v.image}, {"audio", v.audio}, {"embeddedContext", v.embeddedContext}};
}
inline void from_json(const nlohmann::json& j, AgentCapabilities::PromptCapabilities& v) {
    v.image = j.value("image", false);
    v.audio = j.value("audio", false);
    v.embeddedContext = j.value("embeddedContext", false);
}

inline void to_json(nlohmann::json& j, const AgentCapabilities& v) {
    j = nlohmann::json{{"loadSession", v.loadSession}};
    detail::put_opt(j, "promptCapabilities", v.promptCapabilities);
}
inline void from_json(const nlohmann::json& j, AgentCapabilities& v) {
    v.loadSession = j.value("loadSession", false);
    detail::get_opt(j, "promptCapabilities", v.promptCapabilities);
}

// --- initialize ---
inline void to_json(nlohmann::json& j, const InitializeParams& v) {
    j = nlohmann::json{{"protocolVersion", v.protocolVersion}, {"clientCapabilities", v.clientCapabilities}};
    detail::put_opt(j, "clientInfo", v.clientInfo);
}
inline void from_json(const nlohmann::json& j, InitializeParams& v) {
    v.protocolVersion = j.value("protocolVersion", kProtocolVersion);
    if (j.contains("clientCapabilities")) j.at("clientCapabilities").get_to(v.clientCapabilities);
    detail::get_opt(j, "clientInfo", v.clientInfo);
}

inline void to_json(nlohmann::json& j, const InitializeResult& v) {
    j = nlohmann::json{{"protocolVersion", v.protocolVersion}, {"agentCapabilities", v.agentCapabilities}};
    detail::put_opt(j, "agentInfo", v.agentInfo);
}
inline void from_json(const nlohmann::json& j, InitializeResult& v) {
    v.protocolVersion = j.value("protocolVersion", kProtocolVersion);
    if (j.contains("agentCapabilities")) j.at("agentCapabilities").get_to(v.agentCapabilities);
    detail::get_opt(j, "agentInfo", v.agentInfo);
}

// --- Content Blocks ---
inline void to_json(nlohmann::json& j, const TextContent& v) {
    j = nlohmann::json{{"type", "text"}, {"text", v.text}};
}
inline void from_json(const nlohmann::json& j, TextContent& v) {
    j.at("text").get_to(v.text);
}

inline void to_json(nlohmann::json& j, const ImageContent& v) {
    j = nlohmann::json{{"type", "image"}, {"data", v.data}, {"mimeType", v.mimeType}};
    detail::put_opt(j, "uri", v.uri);
}
inline void from_json(const nlohmann::json& j, ImageContent& v) {
    j.at("data").get_to(v.data);
    j.at("mimeType").get_to(v.mimeType);
    detail::get_opt(j, "uri", v.uri);
}

inline void to_json(nlohmann::json& j, const AudioContent& v) {
    j = nlohmann::json{{"type", "audio"}, {"data", v.data}, {"mimeType", v.mimeType}};
}
inline void from_json(const nlohmann::json& j, AudioContent& v) {
    j.at("data").get_to(v.data);
    j.at("mimeType").get_to(v.mimeType);
}

inline void to_json(nlohmann::json& j, const ResourceLinkContent& v) {
    j = nlohmann::json{{"type", "resource_link"}, {"uri", v.uri}, {"name", v.name}};
    detail::put_opt(j, "mimeType", v.mimeType);
    detail::put_opt(j, "description", v.description);
}
inline void from_json(const nlohmann::json& j, ResourceLinkContent& v) {
    j.at("uri").get_to(v.uri);
    j.at("name").get_to(v.name);
    detail::get_opt(j, "mimeType", v.mimeType);
    detail::get_opt(j, "description", v.description);
}

inline void to_json(nlohmann::json& j, const EmbeddedResourcePayload& v) {
    j = nlohmann::json{{"uri", v.uri}};
    detail::put_opt(j, "mimeType", v.mimeType);
    detail::put_opt(j, "text", v.text);
    detail::put_opt(j, "blob", v.blob);
}
inline void from_json(const nlohmann::json& j, EmbeddedResourcePayload& v) {
    j.at("uri").get_to(v.uri);
    detail::get_opt(j, "mimeType", v.mimeType);
    detail::get_opt(j, "text", v.text);
    detail::get_opt(j, "blob", v.blob);
}

inline void to_json(nlohmann::json& j, const EmbeddedResourceContent& v) {
    j = nlohmann::json{{"type", "resource"}, {"resource", v.resource}};
}
inline void from_json(const nlohmann::json& j, EmbeddedResourceContent& v) {
    j.at("resource").get_to(v.resource);
}

inline void to_json(nlohmann::json& j, const ContentBlock& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}

inline void from_json(const nlohmann::json& j, ContentBlock& v) {
    const auto type = j.at("type").get<std::string>();
    if (type == "text") {
        v = j.get<TextContent>();
    } else if (type == "image") {
        v = j.get<ImageContent>();
    } else if (type == "audio") {
        v = j.get<AudioContent>();
    } else if (type == "resource_link") {
        v = j.get<ResourceLinkContent>();
    } else if (type == "resource") {
        v = j.get<EmbeddedResourceContent>();
    } else {
        throw nlohmann::json::type_error::create(302, "Unsupported ContentBlock type: " + type, &j);
    }
}

// --- Sessions, Modes, Models ---
inline void to_json(nlohmann::json& j, const EnvVariable& v) {
    j = nlohmann::json{{"name", v.name}, {"value", v.value}};
}
inline void from_json(const nlohmann::json& j, EnvVariable& v) {
    j.at("name").get_to(v.name);
    j.at("value").get_to(v.value);
}

inline void to_json(nlohmann::json& j, const McpServerStdio& v) {
    j = nlohmann::json{{"name", v.name}, {"command", v.command}, {"args", v.args}, {"env", v.env}};
}
inline void from_json(const nlohmann::json& j, McpServerStdio& v) {
    j.at("name").get_to(v.name);
    j.at("command").get_to(v.command);
    v.args = j.value("args", std::vector<std::string>{});
    v.env = j.value("env", std::vector<EnvVariable>{});
}

inline void to_json(nlohmann::json& j, const SessionMode& v) {
    j = nlohmann::json{{"id", v.id}, {"name", v.name}};
    detail::put_opt(j, "description", v.description);
}
inline void from_json(const nlohmann::json& j, SessionMode& v) {
    j.at("id").get_to(v.id);
    j.at("name").get_to(v.name);
    detail::get_opt(j, "description", v.description);
}

inline void to_json(nlohmann::json& j, const SessionModeState& v) {
    j = nlohmann::json{{"currentModeId", v.currentModeId}, {"availableModes", v.availableModes}};
}
inline void from_json(const nlohmann::json& j, SessionModeState& v) {
    j.at("currentModeId").get_to(v.currentModeId);
    v.availableModes = j.value("availableModes", std::vector<SessionMode>{});
}

inline void to_json(nlohmann::json& j, const ModelInfo& v) {
    j = nlohmann::json{{"modelId", v.modelId}, {"name", v.name}};
    detail::put_opt(j, "description", v.description);
}
inline void from_json(const nlohmann::json& j, ModelInfo& v) {
    j.at("modelId").get_to(v.modelId);
    j.at("name").get_to(v.name);
    detail::get_opt(j, "description", v.description);
}

inline void to_json(nlohmann::json& j, const SessionModelState& v) {
    j = nlohmann::json{{"currentModelId", v.currentModelId}, {"availableModels", v.availableModels}};
}
inline void from_json(const nlohmann::json& j, SessionModelState& v) {
    j.at("currentModelId").get_to(v.currentModelId);
    v.availableModels = j.value("availableModels", std::vector<ModelInfo>{});
}

inline void to_json(nlohmann::json& j, const SessionNewParams& v) {
    j = nlohmann::json{{"cwd", v.cwd}, {"mcpServers", v.mcpServers}};
}
inline void from_json(const nlohmann::json& j, SessionNewParams& v) {
    j.at("cwd").get_to(v.cwd);
    v.mcpServers = j.value("mcpServers", std::vector<McpServerStdio>{});
}

inline void to_json(nlohmann::json& j, const SessionNewResult& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}};
    detail::put_opt(j, "modes", v.modes);
    detail::put_opt(j, "models", v.models);
}
inline void from_json(const nlohmann::json& j, SessionNewResult& v) {
    j.at("sessionId").get_to(v.sessionId);
    detail::get_opt(j, "modes", v.modes);
    detail::get_opt(j, "models", v.models);
}

inline void to_json(nlohmann::json& j, const SessionLoadParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"cwd", v.cwd}, {"mcpServers", v.mcpServers}};
}
inline void from_json(const nlohmann::json& j, SessionLoadParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("cwd").get_to(v.cwd);
    v.mcpServers = j.value("mcpServers", std::vector<McpServerStdio>{});
}

inline void to_json(nlohmann::json& j, const SessionLoadResult& v) {
    j = nlohmann::json::object();
    detail::put_opt(j, "modes", v.modes);
    detail::put_opt(j, "models", v.models);
}
inline void from_json(const nlohmann::json& j, SessionLoadResult& v) {
    detail::get_opt(j, "modes", v.modes);
    detail::get_opt(j, "models", v.models);
}

inline void to_json(nlohmann::json& j, const SessionSetModeParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"modeId", v.modeId}};
}
inline void from_json(const nlohmann::json& j, SessionSetModeParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("modeId").get_to(v.modeId);
}

inline void to_json(nlohmann::json& j, const SessionSetModeResult&) { j = nlohmann::json::object(); }
inline void from_json(const nlohmann::json&, SessionSetModeResult&) {}

inline void to_json(nlohmann::json& j, const SessionSetModelParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"modelId", v.modelId}};
}
inline void from_json(const nlohmann::json& j, SessionSetModelParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("modelId").get_to(v.modelId);
}

inline void to_json(nlohmann::json& j, const SessionSetModelResult&) { j = nlohmann::json::object(); }
inline void from_json(const nlohmann::json&, SessionSetModelResult&) {}

inline void to_json(nlohmann::json& j, const SessionPromptParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"prompt", v.prompt}};
}
inline void from_json(const nlohmann::json& j, SessionPromptParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("prompt").get_to(v.prompt);
}

inline void to_json(nlohmann::json& j, const SessionPromptResult& v) {
    j = nlohmann::json{{"stopReason", v.stopReason}};
}
inline void from_json(const nlohmann::json& j, SessionPromptResult& v) {
    v.stopReason = j.value("stopReason", "end_turn");
}

inline void to_json(nlohmann::json& j, const SessionCancelParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}};
}
inline void from_json(const nlohmann::json& j, SessionCancelParams& v) {
    j.at("sessionId").get_to(v.sessionId);
}

// --- Tool Call Content & Session Updates ---
inline void to_json(nlohmann::json& j, const ToolCallLocation& v) {
    j = nlohmann::json{{"path", v.path}};
    detail::put_opt(j, "line", v.line);
}
inline void from_json(const nlohmann::json& j, ToolCallLocation& v) {
    j.at("path").get_to(v.path);
    detail::get_opt(j, "line", v.line);
}

inline void to_json(nlohmann::json& j, const ToolCallContentItem& v) {
    j = nlohmann::json{{"type", "content"}, {"content", v.content}};
}
inline void from_json(const nlohmann::json& j, ToolCallContentItem& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const ToolCallDiffItem& v) {
    j = nlohmann::json{{"type", "diff"}, {"path", v.path}, {"newText", v.newText}};
    detail::put_opt(j, "oldText", v.oldText);
}
inline void from_json(const nlohmann::json& j, ToolCallDiffItem& v) {
    j.at("path").get_to(v.path);
    j.at("newText").get_to(v.newText);
    detail::get_opt(j, "oldText", v.oldText);
}

inline void to_json(nlohmann::json& j, const ToolCallTerminalItem& v) {
    j = nlohmann::json{{"type", "terminal"}, {"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, ToolCallTerminalItem& v) {
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const ToolCallContent& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}
inline void from_json(const nlohmann::json& j, ToolCallContent& v) {
    const auto type = j.at("type").get<std::string>();
    if (type == "content") {
        v = j.get<ToolCallContentItem>();
    } else if (type == "diff") {
        v = j.get<ToolCallDiffItem>();
    } else if (type == "terminal") {
        v = j.get<ToolCallTerminalItem>();
    } else {
        throw nlohmann::json::type_error::create(302, "Unsupported ToolCallContent type: " + type, &j);
    }
}

inline void to_json(nlohmann::json& j, const UserMessageChunk& v) {
    j = nlohmann::json{{"sessionUpdate", "user_message_chunk"}, {"content", v.content}};
}
inline void from_json(const nlohmann::json& j, UserMessageChunk& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const AgentMessageChunk& v) {
    j = nlohmann::json{{"sessionUpdate", "agent_message_chunk"}, {"content", v.content}};
}
inline void from_json(const nlohmann::json& j, AgentMessageChunk& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const AgentThoughtChunk& v) {
    j = nlohmann::json{{"sessionUpdate", "agent_thought_chunk"}, {"content", v.content}};
}
inline void from_json(const nlohmann::json& j, AgentThoughtChunk& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const ToolCallUpdate& v) {
    j = nlohmann::json{
        {"sessionUpdate", "tool_call"},
        {"toolCallId", v.toolCallId},
        {"title", v.title}
    };
    detail::put_opt(j, "kind", v.kind);
    detail::put_opt(j, "status", v.status);
    detail::put_opt(j, "content", v.content);
    detail::put_opt(j, "locations", v.locations);
}
inline void from_json(const nlohmann::json& j, ToolCallUpdate& v) {
    j.at("toolCallId").get_to(v.toolCallId);
    j.at("title").get_to(v.title);
    detail::get_opt(j, "kind", v.kind);
    detail::get_opt(j, "status", v.status);
    detail::get_opt(j, "content", v.content);
    detail::get_opt(j, "locations", v.locations);
}

inline void to_json(nlohmann::json& j, const ToolCallStatusUpdate& v) {
    j = nlohmann::json{
        {"sessionUpdate", "tool_call_update"},
        {"toolCallId", v.toolCallId}
    };
    detail::put_opt(j, "title", v.title);
    detail::put_opt(j, "kind", v.kind);
    detail::put_opt(j, "status", v.status);
    detail::put_opt(j, "content", v.content);
    detail::put_opt(j, "locations", v.locations);
}
inline void from_json(const nlohmann::json& j, ToolCallStatusUpdate& v) {
    j.at("toolCallId").get_to(v.toolCallId);
    detail::get_opt(j, "title", v.title);
    detail::get_opt(j, "kind", v.kind);
    detail::get_opt(j, "status", v.status);
    detail::get_opt(j, "content", v.content);
    detail::get_opt(j, "locations", v.locations);
}

inline void to_json(nlohmann::json& j, const PlanEntry& v) {
    j = nlohmann::json{{"content", v.content}, {"priority", v.priority}, {"status", v.status}};
}
inline void from_json(const nlohmann::json& j, PlanEntry& v) {
    j.at("content").get_to(v.content);
    j.at("priority").get_to(v.priority);
    j.at("status").get_to(v.status);
}

inline void to_json(nlohmann::json& j, const PlanUpdate& v) {
    j = nlohmann::json{{"sessionUpdate", "plan"}, {"entries", v.entries}};
}
inline void from_json(const nlohmann::json& j, PlanUpdate& v) {
    v.entries = j.value("entries", std::vector<PlanEntry>{});
}

inline void to_json(nlohmann::json& j, const AvailableCommand& v) {
    j = nlohmann::json{{"name", v.name}, {"description", v.description}};
    detail::put_opt(j, "inputHint", v.inputHint);
}
inline void from_json(const nlohmann::json& j, AvailableCommand& v) {
    j.at("name").get_to(v.name);
    j.at("description").get_to(v.description);
    detail::get_opt(j, "inputHint", v.inputHint);
}

inline void to_json(nlohmann::json& j, const AvailableCommandsUpdate& v) {
    j = nlohmann::json{{"sessionUpdate", "available_commands_update"}, {"availableCommands", v.availableCommands}};
}
inline void from_json(const nlohmann::json& j, AvailableCommandsUpdate& v) {
    v.availableCommands = j.value("availableCommands", std::vector<AvailableCommand>{});
}

inline void to_json(nlohmann::json& j, const SessionUpdatePayload& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}
inline void from_json(const nlohmann::json& j, SessionUpdatePayload& v) {
    const auto kind = j.at("sessionUpdate").get<std::string>();
    if (kind == "user_message_chunk") {
        v = j.get<UserMessageChunk>();
    } else if (kind == "agent_message_chunk") {
        v = j.get<AgentMessageChunk>();
    } else if (kind == "agent_thought_chunk") {
        v = j.get<AgentThoughtChunk>();
    } else if (kind == "tool_call") {
        v = j.get<ToolCallUpdate>();
    } else if (kind == "tool_call_update") {
        v = j.get<ToolCallStatusUpdate>();
    } else if (kind == "plan") {
        v = j.get<PlanUpdate>();
    } else if (kind == "available_commands_update") {
        v = j.get<AvailableCommandsUpdate>();
    } else {
        throw nlohmann::json::type_error::create(302, "Unsupported sessionUpdate: " + kind, &j);
    }
}

inline void to_json(nlohmann::json& j, const SessionUpdateNotification& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"update", v.update}};
}
inline void from_json(const nlohmann::json& j, SessionUpdateNotification& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("update").get_to(v.update);
}

// --- Permissions ---
inline void to_json(nlohmann::json& j, const PermissionOption& v) {
    j = nlohmann::json{{"optionId", v.optionId}, {"name", v.name}, {"kind", v.kind}};
}
inline void from_json(const nlohmann::json& j, PermissionOption& v) {
    j.at("optionId").get_to(v.optionId);
    j.at("name").get_to(v.name);
    j.at("kind").get_to(v.kind);
}

inline void to_json(nlohmann::json& j, const PermissionToolCallInfo& v) {
    j = nlohmann::json{{"toolCallId", v.toolCallId}};
    detail::put_opt(j, "title", v.title);
    detail::put_opt(j, "kind", v.kind);
    detail::put_opt(j, "status", v.status);
    detail::put_opt(j, "content", v.content);
    detail::put_opt(j, "locations", v.locations);
}
inline void from_json(const nlohmann::json& j, PermissionToolCallInfo& v) {
    j.at("toolCallId").get_to(v.toolCallId);
    detail::get_opt(j, "title", v.title);
    detail::get_opt(j, "kind", v.kind);
    detail::get_opt(j, "status", v.status);
    detail::get_opt(j, "content", v.content);
    detail::get_opt(j, "locations", v.locations);
}

inline void to_json(nlohmann::json& j, const RequestPermissionParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"toolCall", v.toolCall}, {"options", v.options}};
}
inline void from_json(const nlohmann::json& j, RequestPermissionParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("toolCall").get_to(v.toolCall);
    v.options = j.value("options", std::vector<PermissionOption>{});
}

inline void to_json(nlohmann::json& j, const PermissionCancelledOutcome&) {
    j = nlohmann::json{{"outcome", "cancelled"}};
}
inline void from_json(const nlohmann::json&, PermissionCancelledOutcome&) {}

inline void to_json(nlohmann::json& j, const PermissionSelectedOutcome& v) {
    j = nlohmann::json{{"outcome", "selected"}, {"optionId", v.optionId}};
}
inline void from_json(const nlohmann::json& j, PermissionSelectedOutcome& v) {
    j.at("optionId").get_to(v.optionId);
}

inline void to_json(nlohmann::json& j, const PermissionOutcome& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}
inline void from_json(const nlohmann::json& j, PermissionOutcome& v) {
    const auto outcome = j.at("outcome").get<std::string>();
    if (outcome == "cancelled") {
        v = j.get<PermissionCancelledOutcome>();
    } else if (outcome == "selected") {
        v = j.get<PermissionSelectedOutcome>();
    } else {
        throw nlohmann::json::type_error::create(302, "Unsupported PermissionOutcome: " + outcome, &j);
    }
}

inline void to_json(nlohmann::json& j, const RequestPermissionResult& v) {
    j = nlohmann::json{{"outcome", v.outcome}};
}
inline void from_json(const nlohmann::json& j, RequestPermissionResult& v) {
    j.at("outcome").get_to(v.outcome);
}

// --- Delegated FS Tools ---
inline void to_json(nlohmann::json& j, const FsReadTextFileParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"path", v.path}};
    detail::put_opt(j, "line", v.line);
    detail::put_opt(j, "limit", v.limit);
}
inline void from_json(const nlohmann::json& j, FsReadTextFileParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("path").get_to(v.path);
    detail::get_opt(j, "line", v.line);
    detail::get_opt(j, "limit", v.limit);
}

inline void to_json(nlohmann::json& j, const FsReadTextFileResult& v) {
    j = nlohmann::json{{"content", v.content}};
}
inline void from_json(const nlohmann::json& j, FsReadTextFileResult& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const FsWriteTextFileParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"path", v.path}, {"content", v.content}};
}
inline void from_json(const nlohmann::json& j, FsWriteTextFileParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("path").get_to(v.path);
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const FsWriteTextFileResult&) { j = nlohmann::json::object(); }
inline void from_json(const nlohmann::json&, FsWriteTextFileResult&) {}

// --- Delegated Terminal Tools ---
inline void to_json(nlohmann::json& j, const TerminalCreateParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"command", v.command}, {"args", v.args}, {"env", v.env}};
    detail::put_opt(j, "cwd", v.cwd);
    detail::put_opt(j, "outputByteLimit", v.outputByteLimit);
}
inline void from_json(const nlohmann::json& j, TerminalCreateParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("command").get_to(v.command);
    v.args = j.value("args", std::vector<std::string>{});
    detail::get_opt(j, "cwd", v.cwd);
    v.env = j.value("env", std::vector<EnvVariable>{});
    detail::get_opt(j, "outputByteLimit", v.outputByteLimit);
}

inline void to_json(nlohmann::json& j, const TerminalCreateResult& v) {
    j = nlohmann::json{{"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, TerminalCreateResult& v) {
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const TerminalOutputParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, TerminalOutputParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const TerminalExitStatus& v) {
    j = nlohmann::json::object();
    detail::put_opt(j, "exitCode", v.exitCode);
    detail::put_opt(j, "signal", v.signal);
}
inline void from_json(const nlohmann::json& j, TerminalExitStatus& v) {
    detail::get_opt(j, "exitCode", v.exitCode);
    detail::get_opt(j, "signal", v.signal);
}

inline void to_json(nlohmann::json& j, const TerminalOutputResult& v) {
    j = nlohmann::json{{"output", v.output}, {"truncated", v.truncated}};
    detail::put_opt(j, "exitStatus", v.exitStatus);
}
inline void from_json(const nlohmann::json& j, TerminalOutputResult& v) {
    j.at("output").get_to(v.output);
    v.truncated = j.value("truncated", false);
    detail::get_opt(j, "exitStatus", v.exitStatus);
}

inline void to_json(nlohmann::json& j, const TerminalWaitForExitParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, TerminalWaitForExitParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const TerminalWaitForExitResult& v) {
    j = nlohmann::json::object();
    detail::put_opt(j, "exitCode", v.exitCode);
    detail::put_opt(j, "signal", v.signal);
}
inline void from_json(const nlohmann::json& j, TerminalWaitForExitResult& v) {
    detail::get_opt(j, "exitCode", v.exitCode);
    detail::get_opt(j, "signal", v.signal);
}

inline void to_json(nlohmann::json& j, const TerminalKillParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, TerminalKillParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const TerminalKillResult&) { j = nlohmann::json::object(); }
inline void from_json(const nlohmann::json&, TerminalKillResult&) {}

inline void to_json(nlohmann::json& j, const TerminalReleaseParams& v) {
    j = nlohmann::json{{"sessionId", v.sessionId}, {"terminalId", v.terminalId}};
}
inline void from_json(const nlohmann::json& j, TerminalReleaseParams& v) {
    j.at("sessionId").get_to(v.sessionId);
    j.at("terminalId").get_to(v.terminalId);
}

inline void to_json(nlohmann::json& j, const TerminalReleaseResult&) { j = nlohmann::json::object(); }
inline void from_json(const nlohmann::json&, TerminalReleaseResult&) {}

} // namespace v1

struct NlohmannCodec {
    static expected<RawEnvelope, RpcError> parse_envelope(std::span<const std::byte> frame) {
        thread_local std::string method_buf;
        thread_local std::string params_buf;
        thread_local std::string result_buf;

        method_buf.clear();
        params_buf.clear();
        result_buf.clear();

        std::string_view sv{reinterpret_cast<const char*>(frame.data()), frame.size()};
        nlohmann::json j = nlohmann::json::parse(sv, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return unexpected(RpcError::parse_error("Malformed JSON-RPC object"));
        }

        if (j.value("jsonrpc", "") != "2.0") {
            return unexpected(RpcError::invalid_request("Missing or invalid jsonrpc: \"2.0\""));
        }

        RawEnvelope env{};

        if (auto it = j.find("id"); it != j.end() && !it->is_null()) {
            if (it->is_number_integer()) {
                env.id = it->get<std::int64_t>();
            } else if (it->is_string()) {
                env.id = it->get<std::string>();
            } else {
                return unexpected(RpcError::invalid_request("Invalid JSON-RPC id type"));
            }
        }

        if (auto it = j.find("method"); it != j.end() && it->is_string()) {
            method_buf = it->get<std::string>();
            env.method = method_buf;
        }

        if (auto it = j.find("params"); it != j.end() && !it->is_null()) {
            params_buf = it->dump();
            env.params_json = params_buf;
        }

        if (auto it = j.find("result"); it != j.end()) {
            result_buf = it->dump();
            env.result_json = result_buf;
        }

        if (auto it = j.find("error"); it != j.end() && it->is_object()) {
            RpcError err{
                .code = it->value("code", -32603),
                .message = it->value("message", "Unknown RPC error"),
            };
            if (auto dit = it->find("data"); dit != it->end() && !dit->is_null()) {
                err.data_json = dit->dump();
            }
            env.error = std::move(err);
        }

        return env;
    }

    template <typename T>
    static expected<T, RpcError> decode(std::string_view json) {
        if (json.empty()) {
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            }
            return unexpected(RpcError::invalid_params("Empty JSON payload"));
        }

        try {
            auto parsed = nlohmann::json::parse(json);
            return parsed.get<T>();
        } catch (const std::exception& ex) {
            return unexpected(RpcError::invalid_params(ex.what()));
        }
    }

    template <typename T>
    static std::string encode_request(const RequestId& id, std::string_view method, const T& val) {
        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"id", detail::id_to_json(id)},
            {"method", method},
            {"params", val},
        };
        return j.dump();
    }

    template <typename T>
    static std::string encode_notification(std::string_view method, const T& val) {
        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", val},
        };
        return j.dump();
    }

    template <typename T>
    static std::string encode_result(const RequestId& id, const T& val) {
        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"id", detail::id_to_json(id)},
            {"result", val},
        };
        return j.dump();
    }

    static std::string encode_error(const RequestId& id, const RpcError& err) {
        nlohmann::json err_obj{
            {"code", err.code},
            {"message", err.message},
        };
        if (err.data_json && !err.data_json->empty()) {
            auto parsed_data = nlohmann::json::parse(*err.data_json, nullptr, false);
            err_obj["data"] = parsed_data.is_discarded() ? nlohmann::json(*err.data_json) : parsed_data;
        }

        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"id", detail::id_to_json(id)},
            {"error", std::move(err_obj)},
        };
        return j.dump();
    }
};

static_assert(JsonCodec<NlohmannCodec>);

} // namespace buraq