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
    j = nlohmann::json{
        {"protocolVersion", v.protocolVersion},
        {"clientCapabilities", v.clientCapabilities}
    };
    detail::put_opt(j, "clientInfo", v.clientInfo);
}

inline void from_json(const nlohmann::json& j, InitializeParams& v) {
    v.protocolVersion = j.value("protocolVersion", kProtocolVersion);
    if (j.contains("clientCapabilities")) j.at("clientCapabilities").get_to(v.clientCapabilities);
    detail::get_opt(j, "clientInfo", v.clientInfo);
}

inline void to_json(nlohmann::json& j, const InitializeResult& v) {
    j = nlohmann::json{
        {"protocolVersion", v.protocolVersion},
        {"agentCapabilities", v.agentCapabilities}
    };
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

inline void to_json(nlohmann::json& j, const ResourceLinkContent& v) {
    j = nlohmann::json{{"type", "resource_link"}, {"uri", v.uri}, {"name", v.name}};
    detail::put_opt(j, "mimeType", v.mimeType);
}

inline void from_json(const nlohmann::json& j, ResourceLinkContent& v) {
    j.at("uri").get_to(v.uri);
    j.at("name").get_to(v.name);
    detail::get_opt(j, "mimeType", v.mimeType);
}

inline void to_json(nlohmann::json& j, const ContentBlock& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}

inline void from_json(const nlohmann::json& j, ContentBlock& v) {
    const auto type = j.at("type").get<std::string>();
    if (type == "text") {
        v = j.get<TextContent>();
    } else if (type == "resource_link") {
        v = j.get<ResourceLinkContent>();
    } else {
        throw nlohmann::json::type_error::create(302, "Unsupported ContentBlock type: " + type, &j);
    }
}

// --- Sessions ---
inline void to_json(nlohmann::json& j, const McpServerStdio& v) {
    j = nlohmann::json{{"name", v.name}, {"command", v.command}, {"args", v.args}};
}

inline void from_json(const nlohmann::json& j, McpServerStdio& v) {
    j.at("name").get_to(v.name);
    j.at("command").get_to(v.command);
    v.args = j.value("args", std::vector<std::string>{});
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
}

inline void from_json(const nlohmann::json& j, SessionNewResult& v) {
    j.at("sessionId").get_to(v.sessionId);
}

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

// --- Session Updates ---
inline void to_json(nlohmann::json& j, const AgentMessageChunk& v) {
    j = nlohmann::json{{"sessionUpdate", "agent_message_chunk"}, {"content", v.content}};
}

inline void from_json(const nlohmann::json& j, AgentMessageChunk& v) {
    j.at("content").get_to(v.content);
}

inline void to_json(nlohmann::json& j, const ToolCallUpdate& v) {
    j = nlohmann::json{
        {"sessionUpdate", "tool_call"},
        {"toolCallId", v.toolCallId},
        {"title", v.title},
        {"status", v.status}
    };
}

inline void from_json(const nlohmann::json& j, ToolCallUpdate& v) {
    j.at("toolCallId").get_to(v.toolCallId);
    j.at("title").get_to(v.title);
    j.at("status").get_to(v.status);
}

inline void to_json(nlohmann::json& j, const SessionUpdatePayload& v) {
    std::visit([&j](const auto& item) { to_json(j, item); }, v);
}

inline void from_json(const nlohmann::json& j, SessionUpdatePayload& v) {
    const auto kind = j.at("sessionUpdate").get<std::string>();
    if (kind == "agent_message_chunk") {
        v = j.get<AgentMessageChunk>();
    } else if (kind == "tool_call") {
        v = j.get<ToolCallUpdate>();
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

inline void to_json(nlohmann::json& j, const FsWriteTextFileResult&) {
    j = nlohmann::json::object();
}

inline void from_json(const nlohmann::json&, FsWriteTextFileResult&) {}

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