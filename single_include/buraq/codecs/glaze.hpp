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

// Unions whose default alternative carries no tag (McpServer: stdio, AuthMethod: agent). Glaze's key-based
// deduction cannot tell them apart when the untagged alternative's keys are a subset of a tagged one, so
// dispatch on the tag explicitly: known tag -> that alternative, missing tag -> the untagged alternative.
namespace buraq::detail {

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

#define BURAQ_GLZ_DEFAULT_TAGGED_READER(VariantType)                                          \
    template <>                                                                               \
    struct glz::detail::from<glz::JSON, VariantType> {                                        \
        template <auto Opts>                                                                  \
        static void op(auto&& value, is_context auto&& ctx, auto&& it, auto&& end) {          \
            glz::raw_json raw{};                                                              \
            glz::detail::read<glz::JSON>::template op<Opts>(raw, ctx, it, end);               \
            if (bool(ctx.error)) [[unlikely]] {                                               \
                return;                                                                       \
            }                                                                                 \
            glz::error_code ec{};                                                             \
            buraq::detail::glz_read_default_tagged(value, raw.str, ec);                       \
            if (ec != glz::error_code::none) {                                                \
                ctx.error = ec;                                                               \
            }                                                                                 \
        }                                                                                     \
    };

BURAQ_GLZ_DEFAULT_TAGGED_READER(buraq::v1::McpServer)
BURAQ_GLZ_DEFAULT_TAGGED_READER(buraq::v1::AuthMethod)

#undef BURAQ_GLZ_DEFAULT_TAGGED_READER

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

namespace buraq {

namespace detail {

struct GlzRpcErrorWire {
    int code{-32603};
    std::string message;
    std::optional<glz::raw_json> data{};
};

struct GlzInboundEnvelope {
    std::string_view jsonrpc{};
    std::optional<RequestId> id{};
    std::string_view method{};
    glz::raw_json_view params{};
    glz::raw_json_view result{};
    std::optional<GlzRpcErrorWire> error{};
};

template <typename T>
struct GlzRequestWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    std::string_view method;
    const T* params{nullptr};
};

template <typename T>
struct GlzNotificationWire {
    std::string_view jsonrpc{"2.0"};
    std::string_view method;
    const T* params{nullptr};
};

template <typename T>
struct GlzResultWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    const T* result{nullptr};
};

struct GlzErrorResponseWire {
    std::string_view jsonrpc{"2.0"};
    RequestId id;
    GlzRpcErrorWire error;
};

} // namespace detail

struct GlazeCodec {
    static constexpr glz::opts kWriteOpts{.skip_null_members = true};
    static constexpr glz::opts kReadOpts{.error_on_unknown_keys = false};

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

    template <typename T>
    static std::string encode_request(const RequestId& id, std::string_view method, const T& val) {
        std::string out;
        detail::GlzRequestWire<T> wire{.id = id, .method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    template <typename T>
    static std::string encode_notification(std::string_view method, const T& val) {
        std::string out;
        detail::GlzNotificationWire<T> wire{.method = method, .params = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

    template <typename T>
    static std::string encode_result(const RequestId& id, const T& val) {
        std::string out;
        detail::GlzResultWire<T> wire{.id = id, .result = &val};
        (void)glz::write<kWriteOpts>(wire, out);
        return out;
    }

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
