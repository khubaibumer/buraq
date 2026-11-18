// ============================================================================
// buraq v0.1.0 - Agent Client Protocol (ACP) v1 SDK for C++20/23
// https://github.com/khubaibumer/buraq
//
// Single-header distribution: buraq/codecs/glaze.hpp
// Requires buraq/acp.hpp from this single_include tree.
//
// GENERATED FILE - DO NOT EDIT. Regenerate with: python3 scripts/amalgamate.py
// SPDX-License-Identifier: Apache-2.0
// ============================================================================
#pragma once

#include "../acp.hpp"

// ---- begin buraq/codecs/glaze.hpp ----
/// @file
/// @brief JSON codec backed by Glaze (`buraq::GlazeCodec`).
///
/// Implements the buraq::JsonCodec concept with Glaze: parses raw JSON-RPC 2.0 envelopes, decodes typed
/// params/results while ignoring unknown keys (such as ACP `_meta`), and encodes requests, notifications,
/// results and errors. Also registers the Glaze metadata needed for the ACP v1 schema types (tagged unions,
/// raw JSON passthrough, renamed members).
///
/// Requires Glaze 4.4.3 or newer (tested with 4.4.3 and 9.1.0). The Glaze customization namespace
/// (`glz::detail` before Glaze 5, `glz` afterwards) is auto-detected; see BURAQ_GLAZE_LEGACY_DETAIL_NAMESPACE.
///
/// Usage: `class MyAgent : public buraq::AgentBase<MyAgent, buraq::GlazeCodec> { ... };`

#include <array>
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

// Tagged-union discriminators required by ACP v1 JSON Schema (single source: buraq::v1::variant_tag)
#define BURAQ_GLZ_TAGGED_VARIANT(VariantType)                                              \
    template <>                                                                             \
    struct glz::meta<VariantType> {                                                         \
        static constexpr std::string_view tag = buraq::v1::variant_tag<VariantType>::key;  \
        static constexpr auto ids = buraq::v1::variant_tag<VariantType>::ids;              \
    };

BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::ContentBlock)
BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::ToolCallContent)
BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::SessionUpdatePayload)
BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::PermissionOutcome)
BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::McpServer)
BURAQ_GLZ_TAGGED_VARIANT(buraq::v1::AuthMethod)

#undef BURAQ_GLZ_TAGGED_VARIANT
/// @endcond

// Unions whose default alternative carries no tag (McpServer: stdio, AuthMethod: agent). Glaze's key-based
// deduction cannot tell them apart when the untagged alternative's keys are a subset of a tagged one, so
// dispatch on the tag explicitly: known tag -> that alternative, missing tag -> the untagged alternative.
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
            if constexpr (requires { alt.type; }) {
                alt.type = Alt{}.type; // tag literal must not alias the temporary input buffer
            }
            out = std::move(alt);
        }
    }
}

/// @brief Decodes a tagged union whose default alternative carries no discriminator.
///
/// A known tag value selects that alternative, a missing tag selects buraq::v1::variant_tag::untagged, and
/// an unknown or non-string tag yields `glz::error_code::no_matching_variant_type`.
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
    std::size_t index = static_cast<std::size_t>(Tag::untagged);
    if (auto it = obj.find(Tag::key); it != obj.end()) {
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
            ec = glz::error_code::no_matching_variant_type;
            return;
        }
    }
    glz_emplace_alternative(value, index, json, ec);
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
    /* Write each alternative as-is: tagged ones carry their own `type`, the default one none */ \
    template <>                                                                               \
    struct BURAQ_GLZ_CUSTOMIZATION_NS::to<glz::JSON, VariantType> {                           \
        template <auto Opts>                                                                  \
        static void op(auto&& value, auto&& ctx, auto&&... args) {                            \
            std::visit(                                                                       \
                [&](const auto& alt) {                                                        \
                    using Alt = std::decay_t<decltype(alt)>;                                  \
                    BURAQ_GLZ_CUSTOMIZATION_NS::to<glz::JSON, Alt>::template op<Opts>(        \
                        alt, ctx, args...);                                                   \
                },                                                                            \
                value);                                                                       \
        }                                                                                     \
    };

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

// `delete` is a C++ keyword; the member is spelled `delete_`
template <>
struct glz::meta<buraq::v1::SessionCapabilities> {
    using T = buraq::v1::SessionCapabilities;
    static constexpr auto value = glz::object("list", &T::list,
                                              "delete", &T::delete_,
                                              "additionalDirectories", &T::additionalDirectories,
                                              "systemPrompt", &T::systemPrompt,
                                              "resume", &T::resume,
                                              "close", &T::close);
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

} // namespace detail

/// @brief buraq::JsonCodec implementation backed by Glaze.
///
/// Stateless; all members are static. Unknown keys are ignored on read and null optionals are omitted on write.
struct GlazeCodec {
    /// Glaze write options: omit null (empty optional) members.
    static constexpr glz::opts kWriteOpts{.skip_null_members = true};
    /// Glaze read options: ignore unknown keys such as ACP `_meta`.
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
// ---- end buraq/codecs/glaze.hpp ----
