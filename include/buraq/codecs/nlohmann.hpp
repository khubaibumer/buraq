#pragma once
/// @file
/// @brief JSON codec backed by nlohmann/json (`buraq::NlohmannCodec`).
///
/// Implements the buraq::JsonCodec concept with nlohmann/json: parses raw JSON-RPC 2.0 envelopes, decodes
/// typed params/results while ignoring unknown keys (such as ACP `_meta`), and encodes requests,
/// notifications, results and errors. Also provides ADL `to_json`/`from_json` overloads for every ACP v1
/// schema type.
///
/// Requires nlohmann/json 3.11.3 or newer.
///
/// Usage: `class MyAgent : public buraq::AgentBase<MyAgent, buraq::NlohmannCodec> { ... };`

#include <cstddef>
#include <cstdint>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

namespace buraq {

namespace detail {

/// @brief Converts a JSON-RPC request id (integer or string) to JSON.
/// @param id Request id.
/// @return JSON number or string.
inline nlohmann::json id_to_json(const RequestId& id) {
    return std::visit([](const auto& v) -> nlohmann::json { return v; }, id);
}

/// @brief Field-level helpers used by the generated nlohmann/json `to_json`/`from_json` overloads.
namespace nl {

/// nlohmann/json document type.
using json = nlohmann::json;

/// @brief Throws std::invalid_argument unless @p j is a JSON object.
/// @param j Value to check.
/// @param type_name Type name used in the exception message.
inline void expect_object(const json& j, const char* type_name) {
    if (!j.is_object()) {
        throw std::invalid_argument(std::string("expected JSON object for ") + type_name);
    }
}

// --- Value writers (overloads for types without an ADL to_json) ---
/// @brief Writes @p v into @p j via its nlohmann/json conversion.
/// @tparam T Value type.
/// @param j Destination.
/// @param v Value.
template <typename T>
void put_value(json& j, const T& v) {
    j = v;
}
/// @brief Writes a string view as a JSON string.
/// @param j Destination.
/// @param v Value.
inline void put_value(json& j, std::string_view v) { j = std::string(v); }
/// @brief Writes a request id as a JSON number or string.
/// @param j Destination.
/// @param v Value.
inline void put_value(json& j, const RequestId& v) { j = id_to_json(v); }
/// @brief Writes a session config value (boolean or string).
/// @param j Destination.
/// @param v Value.
inline void put_value(json& j, const v1::SessionConfigValue& v) {
    std::visit([&j](const auto& x) { j = x; }, v);
}

// --- Value readers ---
/// @brief Reads @p j into @p v via its nlohmann/json conversion.
/// @tparam T Value type.
/// @param j Source.
/// @param v Destination.
template <typename T>
void get_value(const json& j, T& v) {
    j.get_to(v);
}
/// @brief No-op: string-view discriminators are fixed by the alternative type.
inline void get_value(const json&, std::string_view&) {} // discriminator: fixed by the alternative type
/// @brief Reads a request id; throws std::invalid_argument unless it is an integer or a string.
/// @param j Source.
/// @param v Destination.
inline void get_value(const json& j, RequestId& v) {
    if (j.is_number_integer()) {
        v = j.get<std::int64_t>();
    } else if (j.is_string()) {
        v = j.get<std::string>();
    } else {
        throw std::invalid_argument("JSON-RPC request id must be an integer or a string");
    }
}
/// @brief Reads a session config value: a JSON boolean, otherwise a string.
/// @param j Source.
/// @param v Destination.
inline void get_value(const json& j, v1::SessionConfigValue& v) {
    if (j.is_boolean()) {
        v = j.get<bool>();
    } else {
        v = j.get<std::string>();
    }
}

// --- Object fields: absent/null keys keep defaults (mirrors GlazeCodec), unknown keys are ignored ---
/// @brief Writes member @p key of object @p j.
/// @tparam T Value type.
/// @param j Destination object.
/// @param key Member name.
/// @param v Value.
template <typename T>
void put(json& j, const char* key, const T& v) {
    put_value(j[key], v);
}
/// @brief Writes member @p key only when @p v holds a value.
/// @tparam T Value type.
/// @param j Destination object.
/// @param key Member name.
/// @param v Optional value.
template <typename T>
void put(json& j, const char* key, const std::optional<T>& v) {
    if (v) put_value(j[key], *v);
}

/// @brief Reads member @p key into @p v; an absent or null member keeps the current value.
/// @tparam T Value type.
/// @param j Source object.
/// @param key Member name.
/// @param v Destination.
template <typename T>
void get(const json& j, const char* key, T& v) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) get_value(*it, v);
}
/// @brief Reads optional member @p key; an absent or null member resets @p v.
/// @tparam T Value type.
/// @param j Source object.
/// @param key Member name.
/// @param v Destination.
template <typename T>
void get(const json& j, const char* key, std::optional<T>& v) {
    if (auto it = j.find(key); it != j.end() && !it->is_null()) {
        T tmp{};
        get_value(*it, tmp);
        v = std::move(tmp);
    } else {
        v.reset();
    }
}

// --- Tagged unions (discriminators from buraq::v1::variant_tag) ---
/// @brief Assigns alternative @p index of @p v from @p j.
/// @tparam Variant std::variant type.
/// @tparam I Alternative indices.
/// @param j Source.
/// @param v Destination.
/// @param index Alternative to read.
template <typename Variant, std::size_t... I>
void read_alternative(const json& j, Variant& v, std::size_t index, std::index_sequence<I...>) {
    ((index == I ? void(v = j.get<std::variant_alternative_t<I, Variant>>()) : void()), ...);
}

/// @brief Reads a tagged union, selecting the alternative by its discriminator.
///
/// A missing or null tag selects the untagged alternative if the union has one; otherwise, as for an
/// unknown tag, std::invalid_argument is thrown.
/// @tparam Variant std::variant with a buraq::v1::variant_tag specialization.
/// @param j Source object.
/// @param v Destination.
/// @param type_name Type name used in error messages.
template <typename Variant>
void read_tagged(const json& j, Variant& v, const char* type_name) {
    using Tag = v1::variant_tag<Variant>;
    expect_object(j, type_name);
    std::size_t index = Tag::ids.size();
    const auto it = j.find(std::string(Tag::key));
    if (it == j.end() || it->is_null()) {
        if constexpr (Tag::untagged >= 0) {
            index = static_cast<std::size_t>(Tag::untagged);
        } else {
            throw std::invalid_argument(std::string(type_name) + ": missing \"" + std::string(Tag::key) + "\"");
        }
    } else {
        const auto tag = it->template get<std::string>();
        for (std::size_t i = 0; i < Tag::ids.size(); ++i) {
            if (Tag::ids[i] == tag) index = i;
        }
        if (index == Tag::ids.size()) {
            throw std::invalid_argument(std::string("Unsupported ") + type_name + " " + std::string(Tag::key) +
                                        ": " + tag);
        }
    }
    read_alternative(j, v, index, std::make_index_sequence<std::variant_size_v<Variant>>{});
}

/// @brief Writes the active alternative of a tagged union (each alternative carries its own tag).
/// @tparam Variant std::variant type.
/// @param j Destination.
/// @param v Value.
template <typename Variant>
void write_tagged(json& j, const Variant& v) {
    std::visit([&j](const auto& alt) { j = alt; }, v);
}

} // namespace nl

} // namespace detail

// Raw JSON passthrough (empty text encodes as `null`)
/// @brief Serializes RawJson by parsing its text; empty text encodes as `null`.
/// @param j Destination.
/// @param v Value.
inline void to_json(nlohmann::json& j, const RawJson& v) {
    j = v.json.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(v.json);
}
/// @brief Captures any JSON value as its serialized text.
/// @param j Source.
/// @param v Destination.
inline void from_json(const nlohmann::json& j, RawJson& v) {
    v.json = j.dump();
}

/// @cond INTERNAL
#define BURAQ_NL_EXPAND(x) x
#define BURAQ_NL_FE_1(M, a) M(a)
#define BURAQ_NL_FE_2(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_1(M, __VA_ARGS__))
#define BURAQ_NL_FE_3(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_2(M, __VA_ARGS__))
#define BURAQ_NL_FE_4(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_3(M, __VA_ARGS__))
#define BURAQ_NL_FE_5(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_4(M, __VA_ARGS__))
#define BURAQ_NL_FE_6(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_5(M, __VA_ARGS__))
#define BURAQ_NL_FE_7(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_6(M, __VA_ARGS__))
#define BURAQ_NL_FE_8(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_7(M, __VA_ARGS__))
#define BURAQ_NL_FE_9(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_8(M, __VA_ARGS__))
#define BURAQ_NL_FE_10(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_9(M, __VA_ARGS__))
#define BURAQ_NL_FE_11(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_10(M, __VA_ARGS__))
#define BURAQ_NL_FE_12(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_11(M, __VA_ARGS__))
#define BURAQ_NL_FE_13(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_12(M, __VA_ARGS__))
#define BURAQ_NL_FE_14(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_13(M, __VA_ARGS__))
#define BURAQ_NL_FE_15(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_14(M, __VA_ARGS__))
#define BURAQ_NL_FE_16(M, a, ...) M(a) BURAQ_NL_EXPAND(BURAQ_NL_FE_15(M, __VA_ARGS__))
#define BURAQ_NL_GET_FE(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, NAME, ...) NAME
#define BURAQ_NL_FOR_EACH(M, ...) \
    BURAQ_NL_EXPAND(BURAQ_NL_GET_FE(__VA_ARGS__, BURAQ_NL_FE_16, BURAQ_NL_FE_15, BURAQ_NL_FE_14, BURAQ_NL_FE_13, BURAQ_NL_FE_12, BURAQ_NL_FE_11, BURAQ_NL_FE_10, BURAQ_NL_FE_9, BURAQ_NL_FE_8, BURAQ_NL_FE_7, BURAQ_NL_FE_6, BURAQ_NL_FE_5, BURAQ_NL_FE_4, BURAQ_NL_FE_3, BURAQ_NL_FE_2, BURAQ_NL_FE_1)(M, __VA_ARGS__))

#define BURAQ_NL_PUT(field) ::buraq::detail::nl::put(j, #field, v.field);
#define BURAQ_NL_GET(field) ::buraq::detail::nl::get(j, #field, v.field);

#define BURAQ_NL_TYPE(Type, ...)                                                 \
    inline void to_json(nlohmann::json& j, const Type& v) {                     \
        j = nlohmann::json::object();                                           \
        BURAQ_NL_EXPAND(BURAQ_NL_FOR_EACH(BURAQ_NL_PUT, __VA_ARGS__))           \
    }                                                                           \
    inline void from_json(const nlohmann::json& j, Type& v) {                   \
        ::buraq::detail::nl::expect_object(j, #Type);                           \
        BURAQ_NL_EXPAND(BURAQ_NL_FOR_EACH(BURAQ_NL_GET, __VA_ARGS__))           \
    }

// Field-less types: encode as `{}`, accept `{}` or `null`
#define BURAQ_NL_EMPTY(Type)                                                     \
    inline void to_json(nlohmann::json& j, const Type&) { j = nlohmann::json::object(); } \
    inline void from_json(const nlohmann::json& j, Type&) {                     \
        if (!j.is_null()) ::buraq::detail::nl::expect_object(j, #Type);         \
    }

#define BURAQ_NL_TAGGED(Type)                                                    \
    inline void to_json(nlohmann::json& j, const Type& v) { ::buraq::detail::nl::write_tagged(j, v); } \
    inline void from_json(const nlohmann::json& j, Type& v) { ::buraq::detail::nl::read_tagged(j, v, #Type); }

/// @endcond
namespace v1 {

// Tagged unions are recursive (ContentBlock inside ToolCallContent, etc.): declare up front
/// @brief Serializes a ContentBlock (tagged by `type`).
inline void to_json(nlohmann::json& j, const ContentBlock& v);
/// @brief Deserializes a ContentBlock (tagged by `type`).
inline void from_json(const nlohmann::json& j, ContentBlock& v);
/// @brief Serializes a ToolCallContent (tagged by `type`).
inline void to_json(nlohmann::json& j, const ToolCallContent& v);
/// @brief Deserializes a ToolCallContent (tagged by `type`).
inline void from_json(const nlohmann::json& j, ToolCallContent& v);
/// @brief Serializes a SessionUpdatePayload (tagged by `sessionUpdate`).
inline void to_json(nlohmann::json& j, const SessionUpdatePayload& v);
/// @brief Deserializes a SessionUpdatePayload (tagged by `sessionUpdate`).
inline void from_json(const nlohmann::json& j, SessionUpdatePayload& v);
/// @brief Serializes a PermissionOutcome (tagged by `outcome`).
inline void to_json(nlohmann::json& j, const PermissionOutcome& v);
/// @brief Deserializes a PermissionOutcome (tagged by `outcome`).
inline void from_json(const nlohmann::json& j, PermissionOutcome& v);
/// @brief Serializes a McpServer (untagged stdio by default).
inline void to_json(nlohmann::json& j, const McpServer& v);
/// @brief Deserializes a McpServer (missing `type` selects stdio).
inline void from_json(const nlohmann::json& j, McpServer& v);
/// @brief Serializes an AuthMethod (untagged agent method by default).
inline void to_json(nlohmann::json& j, const AuthMethod& v);
/// @brief Deserializes an AuthMethod (missing `type` selects the agent method).
inline void from_json(const nlohmann::json& j, AuthMethod& v);

// --- Capabilities & Metadata ---
BURAQ_NL_EMPTY(CapabilityMarker)
BURAQ_NL_TYPE(FileSystemCapabilities, readTextFile, writeTextFile)
BURAQ_NL_TYPE(SessionConfigOptionsCapabilities, boolean)
BURAQ_NL_TYPE(ClientSessionCapabilities, compaction, configOptions, notices)
BURAQ_NL_TYPE(ClientAuthCapabilities, terminal)
BURAQ_NL_TYPE(ElicitationCapabilities, form, url)
BURAQ_NL_TYPE(ClientCapabilities, fs, terminal, session, auth, elicitation)
BURAQ_NL_TYPE(PromptCapabilities, image, audio, embeddedContext)
BURAQ_NL_TYPE(McpCapabilities, http, sse)
BURAQ_NL_TYPE(SystemPromptModeCapabilities, override)
BURAQ_NL_TYPE(SystemPromptCapabilities, mode)

// `delete` is a C++ keyword; the member is spelled `delete_`
/// @brief Serializes SessionCapabilities, mapping member `delete_` to key `delete`.
inline void to_json(nlohmann::json& j, const SessionCapabilities& v) {
    j = nlohmann::json::object();
    ::buraq::detail::nl::put(j, "list", v.list);
    ::buraq::detail::nl::put(j, "delete", v.delete_);
    ::buraq::detail::nl::put(j, "additionalDirectories", v.additionalDirectories);
    ::buraq::detail::nl::put(j, "systemPrompt", v.systemPrompt);
    ::buraq::detail::nl::put(j, "resume", v.resume);
    ::buraq::detail::nl::put(j, "close", v.close);
}
/// @brief Deserializes SessionCapabilities, mapping key `delete` to member `delete_`.
inline void from_json(const nlohmann::json& j, SessionCapabilities& v) {
    ::buraq::detail::nl::expect_object(j, "SessionCapabilities");
    ::buraq::detail::nl::get(j, "list", v.list);
    ::buraq::detail::nl::get(j, "delete", v.delete_);
    ::buraq::detail::nl::get(j, "additionalDirectories", v.additionalDirectories);
    ::buraq::detail::nl::get(j, "systemPrompt", v.systemPrompt);
    ::buraq::detail::nl::get(j, "resume", v.resume);
    ::buraq::detail::nl::get(j, "close", v.close);
}

BURAQ_NL_TYPE(AgentAuthCapabilities, logout)
BURAQ_NL_TYPE(AgentCapabilities, loadSession, promptCapabilities, mcpCapabilities, sessionCapabilities, auth)
BURAQ_NL_TYPE(ImplementationInfo, name, version, title)

// --- Authentication ---
BURAQ_NL_TYPE(AuthMethodAgent, id, name, description)
BURAQ_NL_TYPE(AuthMethodTerminal, type, id, name, description, args, env)
BURAQ_NL_TAGGED(AuthMethod)
BURAQ_NL_TYPE(AuthenticateParams, methodId)
BURAQ_NL_EMPTY(AuthenticateResult)
BURAQ_NL_EMPTY(LogoutParams)
BURAQ_NL_EMPTY(LogoutResult)

// --- initialize ---
BURAQ_NL_TYPE(InitializeParams, protocolVersion, clientCapabilities, clientInfo)
BURAQ_NL_TYPE(InitializeResult, protocolVersion, agentCapabilities, authMethods, agentInfo)

// --- Content Blocks ---
BURAQ_NL_TYPE(Annotations, audience, lastModified, priority)
BURAQ_NL_TYPE(TextContent, type, text, annotations)
BURAQ_NL_TYPE(ImageContent, type, data, mimeType, uri, annotations)
BURAQ_NL_TYPE(AudioContent, type, data, mimeType, annotations)
BURAQ_NL_TYPE(ResourceLinkContent, type, uri, name, mimeType, description, title, size, annotations)
BURAQ_NL_TYPE(EmbeddedResourcePayload, uri, mimeType, text, blob)
BURAQ_NL_TYPE(EmbeddedResourceContent, type, resource, annotations)
BURAQ_NL_TAGGED(ContentBlock)

// --- MCP servers ---
BURAQ_NL_TYPE(EnvVariable, name, value)
BURAQ_NL_TYPE(HttpHeader, name, value)
BURAQ_NL_TYPE(McpServerHttp, type, name, url, headers)
BURAQ_NL_TYPE(McpServerSse, type, name, url, headers)
BURAQ_NL_TYPE(McpServerStdio, name, command, args, env)
BURAQ_NL_TAGGED(McpServer)

// --- Session configuration ---
BURAQ_NL_TYPE(SessionMode, id, name, description)
BURAQ_NL_TYPE(SessionModeState, currentModeId, availableModes)
BURAQ_NL_TYPE(ModelInfo, modelId, name, description)
BURAQ_NL_TYPE(SessionModelState, currentModelId, availableModels)
BURAQ_NL_TYPE(SessionConfigSelectOption, value, name, description)
BURAQ_NL_TYPE(SessionConfigSelectItem, value, name, description, group, options)
BURAQ_NL_TYPE(SessionConfigOption, id, name, description, category, type, currentValue, options)
BURAQ_NL_TYPE(SystemPrompt, mode, content)

// --- Sessions ---
BURAQ_NL_TYPE(SessionNewParams, cwd, mcpServers, additionalDirectories, systemPrompt)
BURAQ_NL_TYPE(SessionNewResult, sessionId, modes, configOptions, models)
BURAQ_NL_TYPE(SessionLoadParams, sessionId, cwd, mcpServers, additionalDirectories, systemPrompt)
BURAQ_NL_TYPE(SessionLoadResult, modes, configOptions, models)
BURAQ_NL_TYPE(SessionResumeParams, sessionId, cwd, mcpServers, additionalDirectories, systemPrompt)
BURAQ_NL_TYPE(SessionResumeResult, modes, configOptions, models)
BURAQ_NL_TYPE(SessionListParams, cwd, cursor)
BURAQ_NL_TYPE(SessionInfo, sessionId, cwd, additionalDirectories, title, updatedAt)
BURAQ_NL_TYPE(SessionListResult, sessions, nextCursor)
BURAQ_NL_TYPE(SessionDeleteParams, sessionId)
BURAQ_NL_EMPTY(SessionDeleteResult)
BURAQ_NL_TYPE(SessionCloseParams, sessionId)
BURAQ_NL_EMPTY(SessionCloseResult)
BURAQ_NL_TYPE(SessionSetModeParams, sessionId, modeId)
BURAQ_NL_EMPTY(SessionSetModeResult)
BURAQ_NL_TYPE(SessionSetConfigOptionParams, sessionId, configId, type, value)
BURAQ_NL_TYPE(SessionSetConfigOptionResult, configOptions)
BURAQ_NL_TYPE(SessionSetModelParams, sessionId, modelId)
BURAQ_NL_EMPTY(SessionSetModelResult)
BURAQ_NL_TYPE(SessionPromptParams, sessionId, prompt)
BURAQ_NL_TYPE(SessionPromptResult, stopReason)
BURAQ_NL_TYPE(SessionCancelParams, sessionId)
BURAQ_NL_TYPE(CancelRequestParams, requestId)

// --- session/update ---
BURAQ_NL_TYPE(UserMessageChunk, sessionUpdate, content, messageId)
BURAQ_NL_TYPE(AgentMessageChunk, sessionUpdate, content, messageId)
BURAQ_NL_TYPE(AgentThoughtChunk, sessionUpdate, content, messageId)
BURAQ_NL_TYPE(ToolCallLocation, path, line)
BURAQ_NL_TYPE(ToolCallContentItem, type, content)
BURAQ_NL_TYPE(ToolCallDiffItem, type, path, oldText, newText)
BURAQ_NL_TYPE(ToolCallTerminalItem, type, terminalId)
BURAQ_NL_TAGGED(ToolCallContent)
BURAQ_NL_TYPE(ToolCallUpdate, sessionUpdate, toolCallId, title, name, kind, status, content, locations, rawInput, rawOutput)
BURAQ_NL_TYPE(ToolCallStatusUpdate, sessionUpdate, toolCallId, title, name, kind, status, content, locations, rawInput, rawOutput)
BURAQ_NL_TYPE(PlanEntry, content, priority, status)
BURAQ_NL_TYPE(PlanUpdate, sessionUpdate, entries)
BURAQ_NL_TYPE(AvailableCommandInput, hint)
BURAQ_NL_TYPE(AvailableCommand, name, description, input)
BURAQ_NL_TYPE(AvailableCommandsUpdate, sessionUpdate, availableCommands)
BURAQ_NL_TYPE(CurrentModeUpdate, sessionUpdate, currentModeId)
BURAQ_NL_TYPE(ConfigOptionUpdate, sessionUpdate, configOptions)
BURAQ_NL_TYPE(SessionInfoUpdate, sessionUpdate, title, updatedAt)
BURAQ_NL_TYPE(Cost, amount, currency)
BURAQ_NL_TYPE(UsageUpdate, sessionUpdate, used, size, cost)
BURAQ_NL_TYPE(NoticeUpdate, sessionUpdate, severity, title, description)
BURAQ_NL_TYPE(CompactionUpdate, sessionUpdate, compactionId, status, summary, error)
BURAQ_NL_TYPE(CompactionSummaryChunk, sessionUpdate, compactionId, content)
BURAQ_NL_TAGGED(SessionUpdatePayload)
BURAQ_NL_TYPE(SessionUpdateNotification, sessionId, update)

// --- Permissions ---
BURAQ_NL_TYPE(PermissionOption, optionId, name, kind)
BURAQ_NL_TYPE(PermissionToolCallInfo, toolCallId, title, name, kind, status, content, locations, rawInput, rawOutput)
BURAQ_NL_TYPE(RequestPermissionParams, sessionId, toolCall, options)
BURAQ_NL_TYPE(PermissionCancelledOutcome, outcome)
BURAQ_NL_TYPE(PermissionSelectedOutcome, outcome, optionId)
BURAQ_NL_TAGGED(PermissionOutcome)
BURAQ_NL_TYPE(RequestPermissionResult, outcome)

// --- Elicitation ---
BURAQ_NL_TYPE(CreateElicitationParams, mode, message, requestedSchema, elicitationId, url, sessionId, toolCallId, requestId)
BURAQ_NL_TYPE(CreateElicitationResult, action, content)
BURAQ_NL_TYPE(CompleteElicitationParams, elicitationId)

// --- Filesystem ---
BURAQ_NL_TYPE(FsReadTextFileParams, sessionId, path, line, limit)
BURAQ_NL_TYPE(FsReadTextFileResult, content)
BURAQ_NL_TYPE(FsWriteTextFileParams, sessionId, path, content)
BURAQ_NL_EMPTY(FsWriteTextFileResult)

// --- Terminal ---
BURAQ_NL_TYPE(TerminalCreateParams, sessionId, command, args, cwd, env, outputByteLimit)
BURAQ_NL_TYPE(TerminalCreateResult, terminalId)
BURAQ_NL_TYPE(TerminalOutputParams, sessionId, terminalId)
BURAQ_NL_TYPE(TerminalExitStatus, exitCode, signal)
BURAQ_NL_TYPE(TerminalOutputResult, output, truncated, exitStatus)
BURAQ_NL_TYPE(TerminalWaitForExitParams, sessionId, terminalId)
BURAQ_NL_TYPE(TerminalWaitForExitResult, exitCode, signal)
BURAQ_NL_TYPE(TerminalKillParams, sessionId, terminalId)
BURAQ_NL_EMPTY(TerminalKillResult)
BURAQ_NL_TYPE(TerminalReleaseParams, sessionId, terminalId)
BURAQ_NL_EMPTY(TerminalReleaseResult)

} // namespace v1

#undef BURAQ_NL_EXPAND
#undef BURAQ_NL_FE_1
#undef BURAQ_NL_FE_2
#undef BURAQ_NL_FE_3
#undef BURAQ_NL_FE_4
#undef BURAQ_NL_FE_5
#undef BURAQ_NL_FE_6
#undef BURAQ_NL_FE_7
#undef BURAQ_NL_FE_8
#undef BURAQ_NL_FE_9
#undef BURAQ_NL_FE_10
#undef BURAQ_NL_FE_11
#undef BURAQ_NL_FE_12
#undef BURAQ_NL_FE_13
#undef BURAQ_NL_FE_14
#undef BURAQ_NL_FE_15
#undef BURAQ_NL_FE_16
#undef BURAQ_NL_GET_FE
#undef BURAQ_NL_FOR_EACH
#undef BURAQ_NL_PUT
#undef BURAQ_NL_GET
#undef BURAQ_NL_TYPE
#undef BURAQ_NL_EMPTY
#undef BURAQ_NL_TAGGED

/// @brief buraq::JsonCodec implementation backed by nlohmann/json.
///
/// Stateless; all members are static. Unknown keys are ignored on read and empty optionals are omitted on write.
struct NlohmannCodec {
    /// @brief Parses the top-level JSON-RPC 2.0 envelope of one frame without decoding the payload.
    ///
    /// `method`, `params` and `result` are copied into thread-local buffers, so the returned views stay valid
    /// only until the next call on the same thread.
    /// @param frame One NDJSON frame (without the trailing newline).
    /// @return The envelope; RpcError::parse_error() when the frame is not a JSON object, or
    /// RpcError::invalid_request() when `jsonrpc` is not "2.0" or `id` is neither integer nor string.
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

    /// @brief Decodes a typed params/result payload.
    /// @tparam T Target type.
    /// @param json Raw JSON text; empty text yields `T{}` when T is default-constructible.
    /// @return The decoded value, or RpcError::invalid_params() on empty input (non-default-constructible T) or
    /// any parse/conversion exception (its `what()` becomes the message).
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

    /// @brief Encodes a JSON-RPC request.
    /// @tparam T Params type.
    /// @param id Request id.
    /// @param method Method name.
    /// @param val Params.
    /// @return Compact JSON text (no trailing newline).
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

    /// @brief Encodes a JSON-RPC notification.
    /// @tparam T Params type.
    /// @param method Method name.
    /// @param val Params.
    /// @return Compact JSON text (no trailing newline).
    template <typename T>
    static std::string encode_notification(std::string_view method, const T& val) {
        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", val},
        };
        return j.dump();
    }

    /// @brief Encodes a JSON-RPC success response.
    /// @tparam T Result type.
    /// @param id Id of the request being answered.
    /// @param val Result.
    /// @return Compact JSON text (no trailing newline).
    template <typename T>
    static std::string encode_result(const RequestId& id, const T& val) {
        nlohmann::json j{
            {"jsonrpc", "2.0"},
            {"id", detail::id_to_json(id)},
            {"result", val},
        };
        return j.dump();
    }

    /// @brief Encodes a JSON-RPC error response.
    /// @param id Id of the request being answered.
    /// @param err Error; a non-empty RpcError::data_json is embedded as JSON `data` (as a string if it is not valid JSON).
    /// @return Compact JSON text (no trailing newline).
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