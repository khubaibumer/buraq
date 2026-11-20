#pragma once
/// @file
/// @brief JSON codec backed by Glaze (`buraq::GlazeCodec`).
///
/// Implements the buraq::JsonCodec concept with Glaze: parses raw JSON-RPC 2.0 envelopes, decodes typed
/// params/results while ignoring unknown keys (ACP `_meta` is preserved in each type's `_meta` member), and encodes requests, notifications,
/// results and errors. Also registers the Glaze metadata needed for the ACP v1 schema types (tagged unions,
/// raw JSON passthrough, renamed members).
///
/// Requires Glaze 4.4.3 or newer (tested with 4.4.3 and 9.1.0). The Glaze customization namespace
/// (`glz::detail` before Glaze 5, `glz` afterwards) is auto-detected; see BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE.
///
/// Usage: `class MyAgent : public buraq::AgentBase<MyAgent, buraq::GlazeCodec> { ... };`

#include <array>
#include <cctype>
#include <cstddef>
#include <map>
#include <glaze/glaze.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"

// Glaze 5 moved its reader/writer customization points from `glz::detail` to `glz`. Glaze has no version macro,
// so detect it by `glaze/core/manage.hpp`, which first shipped in 5.0.1 (define the macro yourself for 5.0.0).
#ifndef BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE
#if __has_include(<glaze/core/manage.hpp>)
/// @brief Selects the Glaze namespace that hosts the `from`/`to` customization points.
///
/// `1` uses `glz::detail` (Glaze < 5), `0` uses `glz` (Glaze >= 5). When not defined by the user it is
/// auto-detected from the presence of `<glaze/core/manage.hpp>` (Glaze 5.0.1+); define it to `0` yourself
/// when building against Glaze 5.0.0.
#define BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE 0
#else
/// @brief Selects the Glaze namespace that hosts the `from`/`to` customization points.
///
/// `1` uses `glz::detail` (Glaze < 5), `0` uses `glz` (Glaze >= 5). When not defined by the user it is
/// auto-detected from the presence of `<glaze/core/manage.hpp>` (Glaze 5.0.1+); define it to `0` yourself
/// when building against Glaze 5.0.0.
#define BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE 1
#endif
#endif

/// @cond INTERNAL
#if BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE
#define BURAQ_GLZ_CUSTOMIZATION_NS glz::detail
#else
#define BURAQ_GLZ_CUSTOMIZATION_NS glz
#endif
/// @endcond

// Tagged unions (discriminators from buraq::v1::variant_tag). Dispatch on the tag explicitly: known tag -> that
// alternative, missing tag -> the untagged alternative (McpServer: stdio, AuthMethod: agent) or an error, any
// other tag -> buraq::v1::Unknown holding the raw object (forward compatibility with newer protocol revisions).
namespace buraq::detail {

/// @brief Reads @p json into alternative @p index of @p out (compile-time recursion over alternatives).
/// @tparam Variant std::variant to fill.
/// @tparam I Alternative index currently examined.
/// @param out Variant assigned only when parsing succeeds.
/// @param index Index of the alternative to parse.
/// @param json Raw JSON object text.
/// @param ec Receives the Glaze error code (`none` on success).
template <typename Variant, std::size_t I = 0>
void glz_emplace_alternative(Variant& out, std::size_t index, std::string_view json, glz::error_code& ec) {
    if constexpr (I < std::variant_size_v<Variant>) {
        if (index != I) {
            glz_emplace_alternative<Variant, I + 1>(out, index, json, ec);
            return;
        }
        using Alt = std::variant_alternative_t<I, Variant>;
        Alt alt{};
        ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(alt, json).ec;
        if (ec == glz::error_code::none) {
            // tag literals must not alias the temporary input buffer
            if constexpr (requires { alt.type; }) {
                alt.type = Alt{}.type;
            }
            if constexpr (requires { alt.sessionUpdate; }) {
                alt.sessionUpdate = Alt{}.sessionUpdate;
            }
            if constexpr (requires { alt.outcome; }) {
                if constexpr (std::is_same_v<decltype(alt.outcome), std::string_view>) {
                    alt.outcome = Alt{}.outcome;
                }
            }
            out = std::move(alt);
        }
    }
}

/// @brief Decodes a tagged union by its discriminator.
///
/// A known tag value selects that alternative, a missing tag selects buraq::v1::variant_tag::untagged (or fails
/// with `glz::error_code::no_matching_variant_type` when the tag is required), an unrecognized string tag yields
/// buraq::v1::Unknown, and a non-string tag fails.
/// @tparam Variant std::variant with a buraq::v1::variant_tag specialization.
/// @param value Variant to fill.
/// @param json Raw JSON object text.
/// @param ec Receives the Glaze error code (`none` on success).
template <typename Variant>
void glz_read_default_tagged(Variant& value, std::string_view json, glz::error_code& ec) {
    using Tag = buraq::v1::variant_tag<Variant>;
    std::map<std::string, glz::raw_json, std::less<>> obj;
    ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(obj, json).ec;
    if (ec != glz::error_code::none) {
        return;
    }
    auto it = obj.find(Tag::key);
    if (it != obj.end() && it->second.str == "null") {
        it = obj.end();
    }
    if (it == obj.end() && Tag::untagged < 0) {
        ec = glz::error_code::no_matching_variant_type;
        return;
    }
    std::size_t index = static_cast<std::size_t>(Tag::untagged);
    if (it != obj.end()) {
        std::string tag;
        if (glz::read_json(tag, it->second.str)) {
            ec = glz::error_code::no_matching_variant_type;
            return;
        }
        index = Tag::ids.size();
        for (std::size_t i = 0; i < Tag::ids.size(); ++i) {
            if (Tag::ids[i] == tag) {
                index = i;
            }
        }
        if (index == Tag::ids.size()) {
            value = buraq::v1::Unknown{std::move(tag), buraq::RawJson{std::string(json)}};
            return;
        }
    }
    glz_emplace_alternative(value, index, json, ec);
}

/// @brief JSON written for a buraq::v1::Unknown without raw text: `{"<key>":"<tag>"}`.
/// @tparam Variant std::variant with a buraq::v1::variant_tag specialization.
/// @param tag Discriminator value.
/// @return The JSON object text.
template <typename Variant>
std::string glz_unknown_stub(const std::string& tag) {
    return "{" + buraq::detail::json_quote(buraq::v1::variant_tag<Variant>::key) + ":" + buraq::detail::json_quote(tag) + "}";
}

} // namespace buraq::detail

/// @cond INTERNAL
#define BURAQ_GLZ_DEFAULT_TAGGED_CODEC(VariantType)                                          \
    template <>                                                                               \
    struct BURAQ_GLZ_CUSTOMIZATION_NS::from<glz::JSON, VariantType> {                         \
        template <auto Opts>                                                                  \
        static void op(auto&& value, auto&& ctx, auto&& it, auto&& end) {                     \
            glz::raw_json raw{};                                                              \
            BURAQ_GLZ_CUSTOMIZATION_NS::from<glz::JSON, glz::raw_json>::template op<Opts>(    \
                raw, ctx, it, end);                                                           \
            if (bool(ctx.error)) [[unlikely]] {                                               \
                return;                                                                       \
            }                                                                                 \
            glz::error_code ec{};                                                             \
            buraq::detail::glz_read_default_tagged(value, raw.str, ec);                       \
            if (ec != glz::error_code::none) {                                                \
                ctx.error = ec;                                                               \
            }                                                                                 \
        }                                                                                     \
    };                                                                                        \
    /* Write each alternative as-is (tagged ones carry their own tag); Unknown verbatim */      \
    template <>                                                                               \
    struct BURAQ_GLZ_CUSTOMIZATION_NS::to<glz::JSON, VariantType> {                           \
        template <auto Opts>                                                                  \
        static void op(auto&& value, auto&& ctx, auto&&... args) {                            \
            std::visit(                                                                       \
                [&](const auto& alt) {                                                        \
                    using Alt = std::decay_t<decltype(alt)>;                                  \
                    if constexpr (std::is_same_v<Alt, buraq::v1::Unknown>) {                 \
                        const glz::raw_json raw{alt.raw.json.empty()                         \
                            ? buraq::detail::glz_unknown_stub<VariantType>(alt.tag)          \
                            : alt.raw.json};                                                  \
                        BURAQ_GLZ_CUSTOMIZATION_NS::to<glz::JSON, glz::raw_json>::template    \
                            op<Opts>(raw, ctx, args...);                                      \
                    } else {                                                                  \
                        BURAQ_GLZ_CUSTOMIZATION_NS::to<glz::JSON, Alt>::template op<Opts>(    \
                            alt, ctx, args...);                                               \
                    }                                                                         \
                },                                                                            \
                value);                                                                       \
        }                                                                                     \
    };

BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::ContentBlock)
BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::ToolCallContent)
BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::SessionUpdatePayload)
BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::PermissionOutcome)
BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::McpServer)
BURAQ_GLZ_DEFAULT_TAGGED_CODEC(buraq::v1::AuthMethod)

#undef BURAQ_GLZ_DEFAULT_TAGGED_CODEC
#undef BURAQ_GLZ_CUSTOMIZATION_NS

// Raw JSON passthrough (empty text encodes as `null`)
template <>
struct glz::meta<buraq::RawJson> {
    static constexpr auto read_fn = [](buraq::RawJson& self, const glz::raw_json& in) { self.json = in.str; };
    static constexpr auto write_fn = [](const buraq::RawJson& self) {
        return glz::raw_json{self.json.empty() ? std::string("null") : self.json};
    };
    static constexpr auto value = glz::custom<read_fn, write_fn>;
};

// Unknown union alternatives are written by the union codec; standalone they read/write their raw object
template <>
struct glz::meta<buraq::v1::Unknown> {
    static constexpr auto read_fn = [](buraq::v1::Unknown& self, const glz::raw_json& in) { self.raw.json = in.str; };
    static constexpr auto write_fn = [](const buraq::v1::Unknown& self) {
        return glz::raw_json{self.raw.json.empty() ? std::string("{}") : self.raw.json};
    };
    static constexpr auto value = glz::custom<read_fn, write_fn>;
};

// `delete` is a C++ keyword; the member is spelled `delete_`
template <>
struct glz::meta<buraq::v1::SessionCapabilities> {
    using T = buraq::v1::SessionCapabilities;
    static constexpr auto value = glz::object("list", &T::list,
                                              "delete", &T::delete_,
                                              "additionalDirectories", &T::additionalDirectories,
                                              "systemPrompt", &T::systemPrompt,
                                              "resume", &T::resume,
                                              "close", &T::close,
                                              "_meta", &T::_meta);
};
/// @endcond

namespace buraq {

namespace detail {

/// @brief Wire shape of a JSON-RPC error object.
struct GlzRpcErrorWire {
    /// Error code; defaults to -32603 (internal error).
    int code{-32603};
    /// Human-readable error message.
    std::string message;
    /// Optional raw `data` member.
    std::optional<glz::raw_json> data{};
};

/// @brief Wire shape used to parse any inbound JSON-RPC message.
///
/// `params`/`result` are captured as raw JSON views into the input frame.
struct GlzInboundEnvelope {
    /// `jsonrpc` version string (must be "2.0").
    std::string_view jsonrpc{};
    /// Request id, absent for notifications.
    std::optional<RequestId> id{};
    /// Method name, empty for responses.
    std::string_view method{};
    /// Raw `params` JSON (empty when absent).
    glz::raw_json_view params{};
    /// Raw `result` JSON (empty when absent).
    glz::raw_json_view result{};
    /// Error object of an error response.
    std::optional<GlzRpcErrorWire> error{};
};

/// @brief Wire shape of an outbound JSON-RPC request.
/// @tparam T Params type.
template <typename T>
struct GlzRequestWire {
    /// Always "2.0".
    std::string_view jsonrpc{"2.0"};
    /// Request id.
    RequestId id;
    /// Method name.
    std::string_view method;
    /// Params serialized by reference.
    const T* params{nullptr};
};

/// @brief Wire shape of an outbound JSON-RPC notification.
/// @tparam T Params type.
template <typename T>
struct GlzNotificationWire {
    /// Always "2.0".
    std::string_view jsonrpc{"2.0"};
    /// Method name.
    std::string_view method;
    /// Params serialized by reference.
    const T* params{nullptr};
};

/// @brief Wire shape of an outbound JSON-RPC success response.
/// @tparam T Result type.
template <typename T>
struct GlzResultWire {
    /// Always "2.0".
    std::string_view jsonrpc{"2.0"};
    /// Id of the request being answered.
    RequestId id;
    /// Result serialized by reference.
    const T* result{nullptr};
};

/// @brief Wire shape of an outbound JSON-RPC error response.
struct GlzErrorResponseWire {
    /// Always "2.0".
    std::string_view jsonrpc{"2.0"};
    /// Id of the request being answered.
    RequestId id;
    /// Error object.
    GlzRpcErrorWire error;
};

/// @brief Returns @p json with every object member whose value is `null` removed (whitespace is dropped).
///
/// Used by GlazeCodec::decode as a fallback so `"field": null` is treated like an absent field (as the
/// nlohmann/json codec and most ACP SDKs do) instead of failing on non-optional members. Malformed input
/// yields unspecified (still malformed) output.
/// @param json JSON text.
/// @return The filtered JSON text.
inline std::string glz_strip_null_members(std::string_view json) {
    struct Frame {
        bool object;
        bool expect_key;
        bool emitted;
    };
    std::string out;
    out.reserve(json.size());
    std::vector<Frame> stack;
    const auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    const auto skip_ws = [&](std::size_t i) {
        while (i < json.size() && is_ws(json[i])) ++i;
        return i;
    };
    const auto string_end = [&](std::size_t i) { // i at opening quote; returns index past closing quote
        for (++i; i < json.size(); ++i) {
            if (json[i] == '\\') {
                ++i;
            } else if (json[i] == '"') {
                return i + 1;
            }
        }
        return json.size();
    };
    std::size_t i = 0;
    while (i < json.size()) {
        const char c = json[i];
        if (is_ws(c)) {
            ++i;
        } else if (c == '"') {
            const std::size_t end = string_end(i);
            if (!stack.empty() && stack.back().object && stack.back().expect_key) {
                std::size_t v = skip_ws(end);
                if (v < json.size() && json[v] == ':') v = skip_ws(v + 1);
                const bool is_null = json.substr(v, 4) == "null" &&
                                     (v + 4 >= json.size() || !std::isalnum(static_cast<unsigned char>(json[v + 4])));
                stack.back().expect_key = false;
                if (is_null) {
                    i = v + 4;
                    continue;
                }
                if (stack.back().emitted) out += ',';
                stack.back().emitted = true;
                out.append(json.substr(i, end - i));
                out += ':';
                i = v;
            } else {
                out.append(json.substr(i, end - i));
                i = end;
            }
        } else if (c == '{' || c == '[') {
            stack.push_back(Frame{c == '{', true, false});
            out += c;
            ++i;
        } else if (c == '}' || c == ']') {
            if (!stack.empty()) stack.pop_back();
            out += c;
            ++i;
        } else if (c == ',' && !stack.empty() && stack.back().object) {
            stack.back().expect_key = true;
            ++i;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

} // namespace detail

/// @brief buraq::JsonCodec implementation backed by Glaze.
///
/// Stateless; all members are static. Unknown keys are ignored on read and null optionals are omitted on write.
struct GlazeCodec {
    /// Glaze write options: omit null (empty optional) members.
    static constexpr glz::opts kWriteOpts{.skip_null_members = true};
    /// Glaze read options: ignore unknown keys.
    static constexpr glz::opts kReadOpts{.error_on_unknown_keys = false};

    /// @brief Parses the top-level JSON-RPC 2.0 envelope of one frame without decoding the payload.
    /// @param frame One NDJSON frame (without the trailing newline).
    /// @return The envelope, whose views point into @p frame; RpcError::parse_error() for malformed JSON or
    /// RpcError::invalid_request() when `jsonrpc` is not "2.0".
    static expected<RawEnvelope, RpcError> parse_envelope(std::span<const std::byte> frame) {
        std::string_view sv{reinterpret_cast<const char*>(frame.data()), frame.size()};

        detail::GlzInboundEnvelope wire{};
        if (auto ec = glz::read<kReadOpts>(wire, sv); ec) {
            return unexpected(RpcError::parse_error(glz::format_error(ec, sv)));
        }

        if (wire.jsonrpc != "2.0") {
            return unexpected(RpcError::invalid_request("Missing or invalid jsonrpc: \"2.0\""));
        }

        RawEnvelope out{
            .id = std::move(wire.id),
            .method = wire.method,
            .params_json = wire.params.str,
            .result_json = wire.result.str,
        };

        if (wire.error) {
            std::optional<std::string> data_str{};
            if (wire.error->data && !wire.error->data->str.empty()) {
                data_str = std::move(wire.error->data->str);
            }
            out.error = RpcError{
                .code = wire.error->code,
                .message = std::move(wire.error->message),
                .data_json = std::move(data_str),
            };
        }

        return out;
    }

    /// @brief Decodes a typed params/result payload.
    /// @tparam T Target type.
    /// @param json Raw JSON text; empty text yields `T{}` when T is default-constructible.
    /// Object members whose value is `null` are treated as absent (they would otherwise fail on non-optional
    /// members).
    /// @return The decoded value, or RpcError::invalid_params() on empty input (non-default-constructible T) or
    /// a Glaze read error.
    template <typename T>
    static expected<T, RpcError> decode(std::string_view json) {
        if (json.empty()) {
            if constexpr (std::is_default_constructible_v<T>) {
                return T{};
            }
            return unexpected(RpcError::invalid_params("Empty JSON payload"));
        }

        T value{};
        if (auto ec = glz::read<kReadOpts>(value, json); ec) {
            // Treat `"member": null` like an absent member (retry only on failure: keeps the fast path exact)
            if (json.find("null") != std::string_view::npos) {
                const std::string lenient = detail::glz_strip_null_members(json);
                T retry{};
                if (!glz::read<kReadOpts>(retry, lenient)) {
                    return retry;
                }
            }
            return unexpected(RpcError::invalid_params(glz::format_error(ec, json)));
        }
        return value;
    }

    /// @brief Encodes a JSON-RPC request.
    /// @tparam T Params type.
    /// @param id Request id.
    /// @param method Method name.
    /// @param val Params.
    /// @return Compact JSON text (no trailing newline).
    template <typename T>
    static std::string encode_request(const RequestId& id, std::string_view method, const T& val) {
        std::string out;
        detail::GlzRequestWire<T> wire{.id = id, .method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    /// @brief Encodes a JSON-RPC notification.
    /// @tparam T Params type.
    /// @param method Method name.
    /// @param val Params.
    /// @return Compact JSON text (no trailing newline).
    template <typename T>
    static std::string encode_notification(std::string_view method, const T& val) {
        std::string out;
        detail::GlzNotificationWire<T> wire{.method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    /// @brief Encodes a JSON-RPC success response.
    /// @tparam T Result type.
    /// @param id Id of the request being answered.
    /// @param val Result.
    /// @return Compact JSON text (no trailing newline).
    template <typename T>
    static std::string encode_result(const RequestId& id, const T& val) {
        std::string out;
        detail::GlzResultWire<T> wire{.id = id, .result = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    /// @brief Encodes a JSON-RPC error response.
    /// @param id Id of the request being answered.
    /// @param err Error; a non-empty RpcError::data_json is embedded verbatim as raw JSON `data`.
    /// @return Compact JSON text (no trailing newline).
    static std::string encode_error(const RequestId& id, const RpcError& err) {
        std::string out;
        std::optional<glz::raw_json> data{};
        if (err.data_json && !err.data_json->empty()) {
            data = glz::raw_json{*err.data_json};
        }
        detail::GlzErrorResponseWire wire{
            .id = id,
            .error = detail::GlzRpcErrorWire{
                .code = err.code,
                .message = err.message,
                .data = std::move(data),
            },
        };
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }
};

static_assert(JsonCodec<GlazeCodec>);

} // namespace buraq