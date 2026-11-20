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
/// ACP v1 protocol types and helpers.
///
/// Field names mirror the wire format (camelCase) so codecs can reflect them directly. Open string
/// enums (stop reasons, tool kinds, statuses, ...) are kept as `std::string` so values introduced by
/// newer protocol revisions round-trip instead of failing to decode.
namespace buraq::v1 {
    /// ACP protocol version implemented by this SDK (sent/expected in `initialize`).
    inline constexpr std::uint32_t kProtocolVersion = 1;

    /// Alternative of every tagged union holding a variant this SDK does not know (yet).
    ///
    /// Produced when decoding a discriminator value introduced by a newer protocol revision or a custom
    /// extension, so peers keep working instead of rejecting the whole message. `raw` holds the complete
    /// JSON object and is written back verbatim when encoded.
    struct Unknown {
        /// Discriminator value found on the wire (e.g. the unrecognized `sessionUpdate` kind).
        std::string tag{};
        /// The complete JSON object, as received.
        RawJson raw{};

        /// Lexicographic comparison of the tag and raw text.
        auto operator<=>(const Unknown &) const = default;
    };

    // Presence-only capability object (`{}` on the wire means "supported")
    /// Presence-only capability object.
    ///
    /// Serialized as `{}`; its presence (in an `std::optional`) means the capability is supported.
    struct CapabilityMarker {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CapabilityMarker &) const = default;
    };

    // --- Capabilities & Metadata ---
    /// File-system capabilities advertised by the client (`clientCapabilities.fs`).
    struct FileSystemCapabilities {
        /// Client supports `fs/read_text_file`. Defaults to `false`.
        bool readTextFile{false};
        /// Client supports `fs/write_text_file`. Defaults to `false`.
        bool writeTextFile{false};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const FileSystemCapabilities &) const = default;
    };

    /// Client support for session config option kinds (`clientCapabilities.session.configOptions`).
    struct SessionConfigOptionsCapabilities {
        /// Present when the client supports `boolean` config options; optional.
        std::optional<CapabilityMarker> boolean{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionConfigOptionsCapabilities &) const = default;
    };

    /// Session-related capabilities advertised by the client (`clientCapabilities.session`).
    struct ClientSessionCapabilities {
        /// Present when the client supports compaction updates; optional.
        std::optional<CapabilityMarker> compaction{};
        /// Config option kinds the client supports; optional.
        std::optional<SessionConfigOptionsCapabilities> configOptions{};
        /// Present when the client supports `notice` session updates; optional.
        std::optional<CapabilityMarker> notices{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ClientSessionCapabilities &) const = default;
    };

    /// Authentication capabilities advertised by the client (`clientCapabilities.auth`).
    struct ClientAuthCapabilities {
        /// Client supports terminal-based authentication methods. Defaults to `false`.
        bool terminal{false};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ClientAuthCapabilities &) const = default;
    };

    /// Elicitation modes supported by the client (`clientCapabilities.elicitation`).
    struct ElicitationCapabilities {
        /// Present when the client supports form-mode elicitation; optional.
        std::optional<CapabilityMarker> form{};
        /// Present when the client supports URL-mode elicitation; optional.
        std::optional<CapabilityMarker> url{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ElicitationCapabilities &) const = default;
    };

    /// Capabilities the client advertises in the `initialize` request.
    struct ClientCapabilities {
        /// Alias of @ref FileSystemCapabilities.
        using FsCapabilities = FileSystemCapabilities;
        /// File-system capabilities; optional (absent means none).
        std::optional<FileSystemCapabilities> fs{};
        /// Client supports the `terminal/*` methods. Defaults to `false`.
        bool terminal{false};
        /// Session-related capabilities; optional.
        std::optional<ClientSessionCapabilities> session{};
        /// Authentication capabilities; optional.
        std::optional<ClientAuthCapabilities> auth{};
        /// Supported elicitation modes; optional (absent means no elicitation).
        std::optional<ElicitationCapabilities> elicitation{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ClientCapabilities &) const = default;
    };

    /// Content types the agent accepts in `session/prompt` (`agentCapabilities.promptCapabilities`).
    ///
    /// Text and resource links are always supported; these flags opt into the rest.
    struct PromptCapabilities {
        /// Agent accepts image content in prompts. Defaults to `false`.
        bool image{false};
        /// Agent accepts audio content in prompts. Defaults to `false`.
        bool audio{false};
        /// Agent accepts embedded resources in prompts. Defaults to `false`.
        bool embeddedContext{false};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PromptCapabilities &) const = default;
    };

    /// MCP transports the agent can connect to (`agentCapabilities.mcpCapabilities`).
    ///
    /// Stdio MCP servers are always supported.
    struct McpCapabilities {
        /// Agent can connect to HTTP MCP servers. Defaults to `false`.
        bool http{false};
        /// Agent can connect to SSE MCP servers. Defaults to `false`.
        bool sse{false};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const McpCapabilities &) const = default;
    };

    /// System-prompt modes supported by the agent beyond the default `append`.
    struct SystemPromptModeCapabilities {
        /// Present when the agent supports `mode: "override"`; optional.
        std::optional<CapabilityMarker> override{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SystemPromptModeCapabilities &) const = default;
    };

    /// Agent support for client-supplied system prompts (`sessionCapabilities.systemPrompt`).
    struct SystemPromptCapabilities {
        /// Supported system-prompt modes; optional.
        std::optional<SystemPromptModeCapabilities> mode{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SystemPromptCapabilities &) const = default;
    };

    // `delete_` is serialized as "delete" (a C++ keyword)
    /// Optional session methods/features supported by the agent (`agentCapabilities.sessionCapabilities`).
    ///
    /// The `delete_` member is serialized as `"delete"`.
    struct SessionCapabilities {
        /// Present when the agent supports `session/list`; optional.
        std::optional<CapabilityMarker> list{};
        /// Present when the agent supports `session/delete`; serialized as `"delete"`; optional.
        std::optional<CapabilityMarker> delete_{};
        /// Present when the agent accepts `additionalDirectories` on session setup; optional.
        std::optional<CapabilityMarker> additionalDirectories{};
        /// System-prompt support; optional.
        std::optional<SystemPromptCapabilities> systemPrompt{};
        /// Present when the agent supports `session/resume`; optional.
        std::optional<CapabilityMarker> resume{};
        /// Present when the agent supports `session/close`; optional.
        std::optional<CapabilityMarker> close{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionCapabilities &) const = default;
    };

    /// Authentication capabilities advertised by the agent (`agentCapabilities.auth`).
    struct AgentAuthCapabilities {
        /// Present when the agent supports `logout`; optional.
        std::optional<CapabilityMarker> logout{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AgentAuthCapabilities &) const = default;
    };

    /// Capabilities the agent advertises in the `initialize` response.
    struct AgentCapabilities {
        /// Alias of @ref buraq::v1::PromptCapabilities.
        using PromptCapabilities = ::buraq::v1::PromptCapabilities;
        /// Agent supports `session/load`. Defaults to `false`.
        bool loadSession{false};
        /// Accepted prompt content types; optional.
        std::optional<PromptCapabilities> promptCapabilities{};
        /// Supported MCP transports; optional.
        std::optional<McpCapabilities> mcpCapabilities{};
        /// Optional session methods/features; optional.
        std::optional<SessionCapabilities> sessionCapabilities{};
        /// Authentication capabilities; optional.
        std::optional<AgentAuthCapabilities> auth{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AgentCapabilities &) const = default;
    };

    /// Name/version information about a client or agent implementation.
    struct ImplementationInfo {
        /// Programmatic name of the implementation.
        std::string name;
        /// Version string of the implementation.
        std::string version;
        /// Optional human-readable display name.
        std::optional<std::string> title{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ImplementationInfo &) const = default;
    };

    // --- Authentication (tagged union discriminated by "type"; untagged = agent-handled) ---
    /// Authentication method handled entirely by the agent.
    ///
    /// Alternative of @ref AuthMethod selected when the `type` field is absent (or `"agent"`).
    struct AuthMethodAgent {
        /// Unique identifier of the method, passed back in `authenticate`.
        std::string id;
        /// Human-readable name of the method.
        std::string name;
        /// Optional description of the method.
        std::optional<std::string> description{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AuthMethodAgent &) const = default;
    };

    /// Authentication method performed interactively in a terminal (`type: "terminal"`).
    ///
    /// Requires the client to advertise `clientCapabilities.auth.terminal`.
    struct AuthMethodTerminal {
        /// Discriminator; always `"terminal"` on the wire.
        std::string_view type{"terminal"};
        /// Unique identifier of the method, passed back in `authenticate`.
        std::string id;
        /// Human-readable name of the method.
        std::string name;
        /// Optional description of the method.
        std::optional<std::string> description{};
        /// Optional extra command-line arguments for the terminal login flow.
        std::optional<std::vector<std::string> > args{};
        /// Optional extra environment variables for the terminal login flow.
        std::optional<std::map<std::string, std::string> > env{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AuthMethodTerminal &) const = default;
    };

    /// An authentication method advertised in the `initialize` result.
    ///
    /// Discriminated by `type`; see @ref variant_tag<AuthMethod>.
    using AuthMethod = std::variant<
        AuthMethodAgent,
        AuthMethodTerminal,
        Unknown
    >;

    /// Parameters of the `authenticate` request.
    struct AuthenticateParams {
        /// Identifier of an auth method advertised in the `initialize` result; required.
        std::string methodId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AuthenticateParams &) const = default;
    };

    /// Result of the `authenticate` request (empty object).
    struct AuthenticateResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AuthenticateResult &) const = default;
    };

    /// Parameters of the `logout` request (empty object).
    struct LogoutParams {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const LogoutParams &) const = default;
    };

    /// Result of the `logout` request (empty object).
    struct LogoutResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const LogoutResult &) const = default;
    };

    // --- Lifecycle: initialize ---
    /// Parameters of the `initialize` request (client -> agent).
    struct InitializeParams {
        /// Latest protocol version supported by the client. Defaults to @ref kProtocolVersion.
        std::uint32_t protocolVersion{kProtocolVersion};
        /// Capabilities supported by the client.
        ClientCapabilities clientCapabilities{};
        /// Optional information about the client implementation.
        std::optional<ImplementationInfo> clientInfo{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const InitializeParams &) const = default;
    };

    /// Result of the `initialize` request.
    struct InitializeResult {
        /// Protocol version chosen by the agent. Defaults to @ref kProtocolVersion.
        std::uint32_t protocolVersion{kProtocolVersion};
        /// Capabilities supported by the agent.
        AgentCapabilities agentCapabilities{};
        /// Authentication methods the agent supports; empty when none are required.
        std::vector<AuthMethod> authMethods{};
        /// Optional information about the agent implementation.
        std::optional<ImplementationInfo> agentInfo{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const InitializeResult &) const = default;
    };

    // --- Content Blocks (tagged union discriminated by "type") ---
    /// Optional hints attached to content blocks (mirrors MCP annotations).
    struct Annotations {
        /// Optional intended audience: `"user"` and/or `"assistant"`.
        std::optional<std::vector<std::string> > audience{}; // "user", "assistant"
        /// Optional last-modified timestamp (ISO 8601).
        std::optional<std::string> lastModified{};
        /// Optional importance, from 0 (least) to 1 (most).
        std::optional<double> priority{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const Annotations &) const = default;
    };

    /// Plain text content block (`type: "text"`).
    struct TextContent {
        /// Discriminator; always `"text"` on the wire.
        std::string_view type{"text"};
        /// The text.
        std::string text;
        /// Optional hints for the client about how to use or display the content.
        std::optional<Annotations> annotations{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TextContent &) const = default;
    };

    /// Image content block (`type: "image"`).
    ///
    /// Requires `promptCapabilities.image` when sent in a prompt.
    struct ImageContent {
        /// Discriminator; always `"image"` on the wire.
        std::string_view type{"image"};
        /// Base64-encoded image bytes.
        std::string data; // Base64-encoded image bytes
        /// MIME type of the image, e.g. `"image/png"`.
        std::string mimeType; // e.g., "image/png"
        /// Optional URI identifying the image source.
        std::optional<std::string> uri{};
        /// Optional hints for the client about how to use or display the content.
        std::optional<Annotations> annotations{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ImageContent &) const = default;
    };

    /// Audio content block (`type: "audio"`).
    ///
    /// Requires `promptCapabilities.audio` when sent in a prompt.
    struct AudioContent {
        /// Discriminator; always `"audio"` on the wire.
        std::string_view type{"audio"};
        /// Base64-encoded audio bytes.
        std::string data; // Base64-encoded audio bytes
        /// MIME type of the audio, e.g. `"audio/wav"`.
        std::string mimeType; // e.g., "audio/wav"
        /// Optional hints for the client about how to use or display the content.
        std::optional<Annotations> annotations{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AudioContent &) const = default;
    };

    /// Reference to a resource the recipient may fetch (`type: "resource_link"`).
    struct ResourceLinkContent {
        /// Discriminator; always `"resource_link"` on the wire.
        std::string_view type{"resource_link"};
        /// URI of the linked resource.
        std::string uri;
        /// Name of the resource.
        std::string name;
        /// Optional MIME type of the resource.
        std::optional<std::string> mimeType{};
        /// Optional description of the resource.
        std::optional<std::string> description{};
        /// Optional human-readable title.
        std::optional<std::string> title{};
        /// Optional size of the resource in bytes.
        std::optional<std::int64_t> size{};
        /// Optional hints for the client about how to use or display the content.
        std::optional<Annotations> annotations{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ResourceLinkContent &) const = default;
    };

    // Text (`text`) or binary (`blob`, Base64) resource contents
    /// Contents of an embedded resource.
    ///
    /// Text resources set `text`; binary resources set `blob` (Base64).
    struct EmbeddedResourcePayload {
        /// URI of the resource.
        std::string uri;
        /// Optional MIME type of the resource.
        std::optional<std::string> mimeType{};
        /// Text contents; set for text resources.
        std::optional<std::string> text{};
        /// Base64-encoded binary contents; set for binary resources.
        std::optional<std::string> blob{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const EmbeddedResourcePayload &) const = default;
    };

    /// Resource embedded inline in a content block (`type: "resource"`).
    ///
    /// Requires `promptCapabilities.embeddedContext` when sent in a prompt.
    struct EmbeddedResourceContent {
        /// Discriminator; always `"resource"` on the wire.
        std::string_view type{"resource"};
        /// The embedded resource contents.
        EmbeddedResourcePayload resource;
        /// Optional hints for the client about how to use or display the content.
        std::optional<Annotations> annotations{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const EmbeddedResourceContent &) const = default;
    };

    /// A content block (text, image, audio, resource link or embedded resource).
    ///
    /// Discriminated by `type`; see @ref variant_tag<ContentBlock>.
    using ContentBlock = std::variant<
        TextContent,
        ImageContent,
        AudioContent,
        ResourceLinkContent,
        EmbeddedResourceContent,
        Unknown
    >;

    // --- MCP servers (tagged union discriminated by "type"; untagged = stdio) ---
    /// Environment variable passed to a launched process (MCP server or terminal).
    struct EnvVariable {
        /// Variable name.
        std::string name;
        /// Variable value.
        std::string value;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const EnvVariable &) const = default;
    };

    /// HTTP header sent to an HTTP/SSE MCP server.
    struct HttpHeader {
        /// Header name.
        std::string name;
        /// Header value.
        std::string value;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const HttpHeader &) const = default;
    };

    /// MCP server reached over streamable HTTP (`type: "http"`).
    ///
    /// Requires `mcpCapabilities.http`.
    struct McpServerHttp {
        /// Discriminator; always `"http"` on the wire.
        std::string_view type{"http"};
        /// Human-readable server name.
        std::string name;
        /// URL of the MCP server.
        std::string url;
        /// HTTP headers to send with each request; may be empty.
        std::vector<HttpHeader> headers{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const McpServerHttp &) const = default;
    };

    /// MCP server reached over Server-Sent Events (`type: "sse"`).
    ///
    /// Requires `mcpCapabilities.sse`.
    struct McpServerSse {
        /// Discriminator; always `"sse"` on the wire.
        std::string_view type{"sse"};
        /// Human-readable server name.
        std::string name;
        /// URL of the MCP server.
        std::string url;
        /// HTTP headers to send with each request; may be empty.
        std::vector<HttpHeader> headers{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const McpServerSse &) const = default;
    };

    /// MCP server launched as a subprocess speaking stdio.
    ///
    /// Alternative of @ref McpServer selected when the `type` field is absent.
    struct McpServerStdio {
        /// Human-readable server name.
        std::string name;
        /// Path of the executable to launch.
        std::string command;
        /// Command-line arguments; may be empty.
        std::vector<std::string> args{};
        /// Environment variables to set; may be empty.
        std::vector<EnvVariable> env{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const McpServerStdio &) const = default;
    };

    /// An MCP server configuration passed in session setup requests.
    ///
    /// Discriminated by `type`; stdio when absent. See @ref variant_tag<McpServer>.
    using McpServer = std::variant<
        McpServerHttp,
        McpServerSse,
        McpServerStdio,
        Unknown
    >;

    // --- Session configuration ---
    /// A mode the agent can operate in (e.g. "ask", "code").
    struct SessionMode {
        /// Unique identifier of the mode.
        std::string id;
        /// Human-readable name of the mode.
        std::string name;
        /// Optional description of the mode.
        std::optional<std::string> description{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionMode &) const = default;
    };

    /// Current and available session modes, returned when a session is created/loaded.
    struct SessionModeState {
        /// Identifier of the active mode.
        std::string currentModeId;
        /// Modes the session can switch to.
        std::vector<SessionMode> availableModes{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionModeState &) const = default;
    };

    // UNSTABLE: session models are not part of the stable ACP v1 schema
    /// A model the agent can use for a session.
    ///
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    struct ModelInfo {
        /// Unique identifier of the model.
        std::string modelId;
        /// Human-readable name of the model.
        std::string name;
        /// Optional description of the model.
        std::optional<std::string> description{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ModelInfo &) const = default;
    };

    // UNSTABLE: session models are not part of the stable ACP v1 schema
    /// Current and available models of a session.
    ///
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    struct SessionModelState {
        /// Identifier of the active model.
        std::string currentModelId;
        /// Models the session can switch to.
        std::vector<ModelInfo> availableModels{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionModelState &) const = default;
    };

    // Value of a config option: a value id (`type: "select"`) or a boolean (`type: "boolean"`)
    /// Value of a config option: a value id (`type: "select"`) or a boolean (`type: "boolean"`).
    using SessionConfigValue = std::variant<std::string, bool>;

    /// A selectable value of a `select` config option.
    struct SessionConfigSelectOption {
        /// Value identifier, sent back in `session/set_config_option`.
        std::string value;
        /// Human-readable name of the value.
        std::string name;
        /// Optional description of the value.
        std::optional<std::string> description{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionConfigSelectOption &) const = default;
    };

    // Entry of a select option list: either an option (`value` set) or a group (`group` + `options` set)
    /// Entry of a `select` config option's option list.
    ///
    /// Either a single option (`value` set) or a named group (`group` and `options` set).
    struct SessionConfigSelectItem {
        /// Value identifier; set when this entry is a single option.
        std::optional<std::string> value{};
        /// Human-readable name of the option or group.
        std::string name;
        /// Optional description of the option.
        std::optional<std::string> description{};
        /// Group identifier; set when this entry is a group.
        std::optional<std::string> group{};
        /// Options of the group; set when this entry is a group.
        std::optional<std::vector<SessionConfigSelectOption> > options{};

        /// Whether this entry is a group rather than a single option.
        /// @return `true` when `group` is set.
        [[nodiscard]] bool is_group() const noexcept { return group.has_value(); }
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionConfigSelectItem &) const = default;
    };

    /// A session configuration option exposed by the agent (e.g. mode, model, thought level).
    struct SessionConfigOption {
        /// Unique identifier of the option, used in `session/set_config_option`.
        std::string id;
        /// Human-readable name of the option.
        std::string name;
        /// Optional description of the option.
        std::optional<std::string> description{};
        /// Optional semantic category: `"mode"`, `"model"`, `"model_config"`, `"thought_level"`, ... (open set).
        std::optional<std::string> category{}; // "mode", "model", "model_config", "thought_level", ...
        /// Option kind: `"select"` (default) or `"boolean"`.
        std::string type{"select"}; // "select" or "boolean"
        /// Current value: a value id for `select`, a `bool` for `boolean`.
        SessionConfigValue currentValue{std::string{}};
        /// Selectable values; present for `select` options.
        std::optional<std::vector<SessionConfigSelectItem> > options{}; // present for "select"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionConfigOption &) const = default;
    };

    /// Client-supplied system prompt for a session.
    ///
    /// Requires `sessionCapabilities.systemPrompt`.
    struct SystemPrompt {
        /// How the prompt is applied: `"append"` (default) or `"override"`.
        std::string mode{"append"}; // "append", "override"
        /// System prompt content blocks.
        std::vector<ContentBlock> content{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SystemPrompt &) const = default;
    };

    // --- Sessions: new, load, resume, list, delete, close, mode, config, prompt, cancel ---
    /// Parameters of the `session/new` request.
    struct SessionNewParams {
        /// Working directory of the session; MUST be an absolute path.
        std::string cwd;
        /// MCP servers the agent should connect to; may be empty.
        std::vector<McpServer> mcpServers{};
        /// Optional extra workspace directories (absolute paths); requires `sessionCapabilities.additionalDirectories`.
        std::optional<std::vector<std::string> > additionalDirectories{};
        /// Optional client-supplied system prompt.
        std::optional<SystemPrompt> systemPrompt{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionNewParams &) const = default;
    };

    /// Result of the `session/new` request.
    struct SessionNewResult {
        /// Identifier of the newly created session.
        std::string sessionId;
        /// Optional session mode state.
        std::optional<SessionModeState> modes{};
        /// Optional session configuration options.
        std::optional<std::vector<SessionConfigOption> > configOptions{};
        /// Optional session model state.
        /// @note Unstable in ACP: not part of the stable ACP v1 schema.
        std::optional<SessionModelState> models{}; // UNSTABLE
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionNewResult &) const = default;
    };

    /// Parameters of the `session/load` request.
    ///
    /// Requires `agentCapabilities.loadSession`; the agent replays the conversation via `session/update`.
    struct SessionLoadParams {
        /// Identifier of the session to load.
        std::string sessionId;
        /// Working directory of the session; MUST be an absolute path.
        std::string cwd;
        /// MCP servers the agent should connect to; may be empty.
        std::vector<McpServer> mcpServers{};
        /// Optional extra workspace directories (absolute paths).
        std::optional<std::vector<std::string> > additionalDirectories{};
        /// Optional client-supplied system prompt.
        std::optional<SystemPrompt> systemPrompt{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionLoadParams &) const = default;
    };

    /// Result of the `session/load` request.
    struct SessionLoadResult {
        /// Optional session mode state.
        std::optional<SessionModeState> modes{};
        /// Optional session configuration options.
        std::optional<std::vector<SessionConfigOption> > configOptions{};
        /// Optional session model state.
        /// @note Unstable in ACP: not part of the stable ACP v1 schema.
        std::optional<SessionModelState> models{}; // UNSTABLE
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionLoadResult &) const = default;
    };

    /// Parameters of the `session/resume` request.
    ///
    /// Requires `sessionCapabilities.resume`; unlike `session/load`, history is not replayed.
    struct SessionResumeParams {
        /// Identifier of the session to resume.
        std::string sessionId;
        /// Working directory of the session; MUST be an absolute path.
        std::string cwd;
        /// Optional MCP servers the agent should connect to.
        std::optional<std::vector<McpServer> > mcpServers{};
        /// Optional extra workspace directories (absolute paths).
        std::optional<std::vector<std::string> > additionalDirectories{};
        /// Optional client-supplied system prompt.
        std::optional<SystemPrompt> systemPrompt{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionResumeParams &) const = default;
    };

    /// Result of the `session/resume` request.
    struct SessionResumeResult {
        /// Optional session mode state.
        std::optional<SessionModeState> modes{};
        /// Optional session configuration options.
        std::optional<std::vector<SessionConfigOption> > configOptions{};
        /// Optional session model state.
        /// @note Unstable in ACP: not part of the stable ACP v1 schema.
        std::optional<SessionModelState> models{}; // UNSTABLE
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionResumeResult &) const = default;
    };

    /// Parameters of the `session/list` request.
    ///
    /// Requires `sessionCapabilities.list`.
    struct SessionListParams {
        /// Optional filter: only sessions with this working directory (absolute path).
        std::optional<std::string> cwd{};
        /// Optional opaque pagination cursor from a previous `nextCursor`.
        std::optional<std::string> cursor{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionListParams &) const = default;
    };

    /// Summary of a session as returned by `session/list`.
    struct SessionInfo {
        /// Identifier of the session.
        std::string sessionId;
        /// Working directory of the session.
        std::string cwd;
        /// Optional extra workspace directories of the session.
        std::optional<std::vector<std::string> > additionalDirectories{};
        /// Optional human-readable title.
        std::optional<std::string> title{};
        /// Optional last-activity timestamp (ISO 8601).
        std::optional<std::string> updatedAt{}; // ISO 8601
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionInfo &) const = default;
    };

    /// Result of the `session/list` request.
    struct SessionListResult {
        /// Sessions in this page.
        std::vector<SessionInfo> sessions{};
        /// Cursor for the next page; absent when there are no more results.
        std::optional<std::string> nextCursor{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionListResult &) const = default;
    };

    /// Parameters of the `session/delete` request.
    ///
    /// Requires `sessionCapabilities.delete`.
    struct SessionDeleteParams {
        /// Identifier of the session to delete.
        std::string sessionId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionDeleteParams &) const = default;
    };

    /// Result of the `session/delete` request (empty object).
    struct SessionDeleteResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionDeleteResult &) const = default;
    };

    /// Parameters of the `session/close` request.
    ///
    /// Requires `sessionCapabilities.close`.
    struct SessionCloseParams {
        /// Identifier of the session to close.
        std::string sessionId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionCloseParams &) const = default;
    };

    /// Result of the `session/close` request (empty object).
    struct SessionCloseResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionCloseResult &) const = default;
    };

    /// Parameters of the `session/set_mode` request.
    struct SessionSetModeParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the mode to switch to.
        std::string modeId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetModeParams &) const = default;
    };

    /// Result of the `session/set_mode` request (empty object).
    struct SessionSetModeResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetModeResult &) const = default;
    };

    /// Parameters of the `session/set_config_option` request.
    struct SessionSetConfigOptionParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the config option to change.
        std::string configId;
        /// Optional value kind; `"boolean"` when `value` is a `bool`.
        std::optional<std::string> type{}; // "boolean" when `value` is a bool
        /// New value: a value id for `select`, a `bool` for `boolean`.
        SessionConfigValue value{std::string{}};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetConfigOptionParams &) const = default;
    };

    /// Result of the `session/set_config_option` request.
    struct SessionSetConfigOptionResult {
        /// Full, updated set of the session's config options.
        std::vector<SessionConfigOption> configOptions{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetConfigOptionResult &) const = default;
    };

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    /// Parameters of the `session/set_model` request.
    ///
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    struct SessionSetModelParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the model to switch to.
        std::string modelId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetModelParams &) const = default;
    };

    // UNSTABLE: `session/set_model` is not part of the stable ACP v1 schema
    /// Result of the `session/set_model` request (empty object).
    ///
    /// @note Unstable in ACP: not part of the stable ACP v1 schema.
    struct SessionSetModelResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionSetModelResult &) const = default;
    };

    /// Parameters of the `session/prompt` request.
    struct SessionPromptParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Content blocks of the user's message.
        std::vector<ContentBlock> prompt;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionPromptParams &) const = default;
    };

    /// Result of the `session/prompt` request, sent when the turn ends.
    struct SessionPromptResult {
        /// Why the turn ended: `"end_turn"` (default), `"max_tokens"`, `"max_turn_requests"`, `"refusal"` or `"cancelled"` (open set).
        std::string stopReason{"end_turn"}; // "end_turn", "max_tokens", "max_turn_requests", "refusal", "cancelled"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionPromptResult &) const = default;
    };

    /// Parameters of the `session/cancel` notification (client -> agent).
    struct SessionCancelParams {
        /// Identifier of the session whose prompt turn should be cancelled.
        std::string sessionId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionCancelParams &) const = default;
    };

    // `$/cancel_request` (either direction): cancel an in-flight JSON-RPC request
    /// Parameters of the `$/cancel_request` notification (either direction).
    ///
    /// Asks the peer to cancel an in-flight JSON-RPC request.
    struct CancelRequestParams {
        /// Identifier of the request to cancel. Defaults to `0`.
        RequestId requestId{std::int64_t{0}};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CancelRequestParams &) const = default;
    };

    // --- Streaming Updates: session/update (tagged union discriminated by "sessionUpdate") ---
    /// `session/update` payload streaming a chunk of a user message (`sessionUpdate: "user_message_chunk"`).
    ///
    /// Used mainly when replaying history in `session/load`.
    struct UserMessageChunk {
        /// Discriminator; always `"user_message_chunk"` on the wire.
        std::string_view sessionUpdate{"user_message_chunk"};
        /// The content block being streamed.
        ContentBlock content;
        /// Optional identifier grouping chunks that belong to the same message.
        std::optional<std::string> messageId{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const UserMessageChunk &) const = default;
    };

    /// `session/update` payload streaming a chunk of the agent's response (`sessionUpdate: "agent_message_chunk"`).
    struct AgentMessageChunk {
        /// Discriminator; always `"agent_message_chunk"` on the wire.
        std::string_view sessionUpdate{"agent_message_chunk"};
        /// The content block being streamed.
        ContentBlock content;
        /// Optional identifier grouping chunks that belong to the same message.
        std::optional<std::string> messageId{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AgentMessageChunk &) const = default;
    };

    /// `session/update` payload streaming a chunk of the agent's reasoning (`sessionUpdate: "agent_thought_chunk"`).
    struct AgentThoughtChunk {
        /// Discriminator; always `"agent_thought_chunk"` on the wire.
        std::string_view sessionUpdate{"agent_thought_chunk"};
        /// The content block being streamed.
        ContentBlock content;
        /// Optional identifier grouping chunks that belong to the same message.
        std::optional<std::string> messageId{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AgentThoughtChunk &) const = default;
    };

    /// File location affected by a tool call (used for "follow along" UIs).
    struct ToolCallLocation {
        /// Absolute path of the affected file.
        std::string path;
        /// Optional line number within the file.
        std::optional<std::uint32_t> line{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallLocation &) const = default;
    };

    /// Tool call content wrapping a regular content block (`type: "content"`).
    struct ToolCallContentItem {
        /// Discriminator; always `"content"` on the wire.
        std::string_view type{"content"};
        /// The wrapped content block.
        ContentBlock content;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallContentItem &) const = default;
    };

    /// Tool call content describing a file modification (`type: "diff"`).
    struct ToolCallDiffItem {
        /// Discriminator; always `"diff"` on the wire.
        std::string_view type{"diff"};
        /// Absolute path of the modified file.
        std::string path;
        /// Original text; absent for newly created files.
        std::optional<std::string> oldText{};
        /// New text after the modification.
        std::string newText;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallDiffItem &) const = default;
    };

    /// Tool call content embedding a client terminal's live output (`type: "terminal"`).
    struct ToolCallTerminalItem {
        /// Discriminator; always `"terminal"` on the wire.
        std::string_view type{"terminal"};
        /// Identifier of the embedded terminal, as returned by `terminal/create`.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallTerminalItem &) const = default;
    };

    /// Content produced by a tool call (content block, diff or terminal).
    ///
    /// Discriminated by `type`; see @ref variant_tag<ToolCallContent>.
    using ToolCallContent = std::variant<
        ToolCallContentItem,
        ToolCallDiffItem,
        ToolCallTerminalItem,
        Unknown
    >;

    // `sessionUpdate: "tool_call"` (the spec's `ToolCall`): announces a new tool call
    /// `session/update` payload announcing a new tool call (`sessionUpdate: "tool_call"`).
    ///
    /// This is the spec's `ToolCall`; see also the @ref ToolCall alias.
    struct ToolCallUpdate {
        /// Discriminator; always `"tool_call"` on the wire.
        std::string_view sessionUpdate{"tool_call"};
        /// Unique identifier of the tool call within the session.
        std::string toolCallId;
        /// Human-readable title of the tool call.
        std::string title;
        /// Optional programmatic name of the invoked tool.
        std::optional<std::string> name{};
        /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
        std::optional<std::string> kind{};
        // "read", "edit", "delete", "move", "search", "execute", "think", "fetch", "switch_mode", "other"
        /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
        std::optional<std::string> status{}; // "pending", "in_progress", "completed", "failed"
        /// Optional content produced by the tool call.
        std::optional<std::vector<ToolCallContent> > content{};
        /// Optional file locations affected by the tool call.
        std::optional<std::vector<ToolCallLocation> > locations{};
        /// Optional raw input parameters sent to the tool (arbitrary JSON).
        std::optional<RawJson> rawInput{};
        /// Optional raw output returned by the tool (arbitrary JSON).
        std::optional<RawJson> rawOutput{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallUpdate &) const = default;
    };

    /// Spec-named alias of @ref ToolCallUpdate (the `"tool_call"` session update).
    using ToolCall = ToolCallUpdate;

    // `sessionUpdate: "tool_call_update"` (the spec's `ToolCallUpdate`): patches an existing tool call
    /// `session/update` payload patching an existing tool call (`sessionUpdate: "tool_call_update"`).
    ///
    /// This is the spec's `ToolCallUpdate`; only `toolCallId` is required and absent fields are left unchanged.
    struct ToolCallStatusUpdate {
        /// Discriminator; always `"tool_call_update"` on the wire.
        std::string_view sessionUpdate{"tool_call_update"};
        /// Unique identifier of the tool call within the session.
        std::string toolCallId;
        /// Human-readable title of the tool call. Optional.
        std::optional<std::string> title{};
        /// Optional programmatic name of the invoked tool.
        std::optional<std::string> name{};
        /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
        std::optional<std::string> kind{};
        /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
        std::optional<std::string> status{};
        /// Optional content produced by the tool call. Replaces previous content when set.
        std::optional<std::vector<ToolCallContent> > content{};
        /// Optional file locations affected by the tool call.
        std::optional<std::vector<ToolCallLocation> > locations{};
        /// Optional raw input parameters sent to the tool (arbitrary JSON).
        std::optional<RawJson> rawInput{};
        /// Optional raw output returned by the tool (arbitrary JSON).
        std::optional<RawJson> rawOutput{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ToolCallStatusUpdate &) const = default;
    };

    /// One task of an agent execution plan.
    struct PlanEntry {
        /// Human-readable description of the task.
        std::string content;
        /// Relative priority: `"high"`, `"medium"` or `"low"`.
        std::string priority; // "high", "medium", "low"
        /// Execution status: `"pending"`, `"in_progress"` or `"completed"`.
        std::string status; // "pending", "in_progress", "completed"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PlanEntry &) const = default;
    };

    /// `session/update` payload carrying the agent's execution plan (`sessionUpdate: "plan"`).
    struct PlanUpdate {
        /// Discriminator; always `"plan"` on the wire.
        std::string_view sessionUpdate{"plan"};
        /// Complete list of plan entries; replaces any previously sent plan.
        std::vector<PlanEntry> entries{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PlanUpdate &) const = default;
    };

    // Unstructured command input: free text shown as a hint when the user types the command
    /// Unstructured input of a slash command.
    ///
    /// Free text shown as a hint when the user types the command.
    struct AvailableCommandInput {
        /// Hint text shown to the user for the command's input.
        std::string hint;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AvailableCommandInput &) const = default;
    };

    /// A slash command the agent makes available to the user.
    struct AvailableCommand {
        /// Command name (e.g. `create_plan`, invoked as `/create_plan`).
        std::string name;
        /// Human-readable description of the command.
        std::string description;
        /// Optional input specification; absent when the command takes no input.
        std::optional<AvailableCommandInput> input{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AvailableCommand &) const = default;
    };

    /// `session/update` payload listing available slash commands (`sessionUpdate: "available_commands_update"`).
    struct AvailableCommandsUpdate {
        /// Discriminator; always `"available_commands_update"` on the wire.
        std::string_view sessionUpdate{"available_commands_update"};
        /// Complete list of currently available commands.
        std::vector<AvailableCommand> availableCommands{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const AvailableCommandsUpdate &) const = default;
    };

    /// `session/update` payload reporting a session mode change (`sessionUpdate: "current_mode_update"`).
    struct CurrentModeUpdate {
        /// Discriminator; always `"current_mode_update"` on the wire.
        std::string_view sessionUpdate{"current_mode_update"};
        /// Identifier of the now-active mode.
        std::string currentModeId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CurrentModeUpdate &) const = default;
    };

    /// `session/update` payload carrying the session's config options (`sessionUpdate: "config_option_update"`).
    struct ConfigOptionUpdate {
        /// Discriminator; always `"config_option_update"` on the wire.
        std::string_view sessionUpdate{"config_option_update"};
        /// Full, updated set of the session's config options.
        std::vector<SessionConfigOption> configOptions{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const ConfigOptionUpdate &) const = default;
    };

    /// `session/update` payload updating session metadata (`sessionUpdate: "session_info_update"`).
    struct SessionInfoUpdate {
        /// Discriminator; always `"session_info_update"` on the wire.
        std::string_view sessionUpdate{"session_info_update"};
        /// Optional new session title.
        std::optional<std::string> title{};
        /// Optional new last-activity timestamp (ISO 8601).
        std::optional<std::string> updatedAt{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionInfoUpdate &) const = default;
    };

    /// Monetary cost reported in a usage update.
    struct Cost {
        /// Amount in `currency` units. Defaults to `0.0`.
        double amount{0.0};
        /// ISO 4217 currency code, e.g. `"USD"`.
        std::string currency; // ISO 4217, e.g. "USD"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const Cost &) const = default;
    };

    /// `session/update` payload reporting context-window usage (`sessionUpdate: "usage_update"`).
    struct UsageUpdate {
        /// Discriminator; always `"usage_update"` on the wire.
        std::string_view sessionUpdate{"usage_update"};
        /// Tokens currently in the context window. Defaults to `0`.
        std::uint64_t used{0}; // tokens currently in context
        /// Size of the context window in tokens. Defaults to `0`.
        std::uint64_t size{0}; // context window size in tokens
        /// Optional cumulative cost of the session.
        std::optional<Cost> cost{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const UsageUpdate &) const = default;
    };

    /// `session/update` payload carrying a user-facing notice (`sessionUpdate: "notice"`).
    ///
    /// Sent only to clients advertising `clientCapabilities.session.notices`.
    struct NoticeUpdate {
        /// Discriminator; always `"notice"` on the wire.
        std::string_view sessionUpdate{"notice"};
        /// Severity: `"info"` (default), `"warning"` or `"error"`.
        std::string severity{"info"}; // "info", "warning", "error"
        /// Short notice title.
        std::string title;
        /// Optional longer description.
        std::optional<std::string> description{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const NoticeUpdate &) const = default;
    };

    /// `session/update` payload reporting context compaction progress (`sessionUpdate: "compaction_update"`).
    ///
    /// Sent only to clients advertising `clientCapabilities.session.compaction`.
    struct CompactionUpdate {
        /// Discriminator; always `"compaction_update"` on the wire.
        std::string_view sessionUpdate{"compaction_update"};
        /// Identifier of the compaction operation.
        std::string compactionId;
        /// Status: `"in_progress"` (default), `"completed"`, `"failed"` or `"cancelled"`.
        std::string status{"in_progress"}; // "in_progress", "completed", "failed", "cancelled"
        /// Optional summary of the compacted context.
        std::optional<std::vector<ContentBlock> > summary{};
        /// Optional error message when the compaction failed.
        std::optional<std::string> error{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CompactionUpdate &) const = default;
    };

    /// `session/update` payload streaming a chunk of a compaction summary (`sessionUpdate: "compaction_summary_chunk"`).
    struct CompactionSummaryChunk {
        /// Discriminator; always `"compaction_summary_chunk"` on the wire.
        std::string_view sessionUpdate{"compaction_summary_chunk"};
        /// Identifier of the compaction operation the chunk belongs to.
        std::string compactionId;
        /// The summary content being streamed.
        ContentBlock content;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CompactionSummaryChunk &) const = default;
    };

    /// Payload of a `session/update` notification.
    ///
    /// Discriminated by `sessionUpdate`; see @ref variant_tag<SessionUpdatePayload>.
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
        CompactionSummaryChunk,
        Unknown
    >;

    /// Parameters of the `session/update` notification (agent -> client).
    struct SessionUpdateNotification {
        /// Identifier of the session the update belongs to.
        std::string sessionId;
        /// The update payload (discriminated by `sessionUpdate`).
        SessionUpdatePayload update;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const SessionUpdateNotification &) const = default;
    };

    // --- Permissions: session/request_permission (Agent -> Client request) ---
    /// An option presented to the user in a `session/request_permission` request.
    struct PermissionOption {
        /// Unique identifier of the option, returned in the selected outcome.
        std::string optionId;
        /// Human-readable label of the option.
        std::string name;
        /// Option kind: `"allow_once"`, `"allow_always"`, `"reject_once"` or `"reject_always"`.
        std::string kind; // "allow_once", "allow_always", "reject_once", "reject_always"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PermissionOption &) const = default;
    };

    /// Tool call details included in a `session/request_permission` request.
    ///
    /// Same shape as a `tool_call_update` without the discriminator: only `toolCallId` is required.
    struct PermissionToolCallInfo {
        /// Unique identifier of the tool call within the session.
        std::string toolCallId;
        /// Human-readable title of the tool call. Optional.
        std::optional<std::string> title{};
        /// Optional programmatic name of the invoked tool.
        std::optional<std::string> name{};
        /// Optional tool kind: `"read"`, `"edit"`, `"delete"`, `"move"`, `"search"`, `"execute"`, `"think"`, `"fetch"`, `"switch_mode"`, `"other"` (open set).
        std::optional<std::string> kind{};
        /// Optional status: `"pending"`, `"in_progress"`, `"completed"` or `"failed"` (open set).
        std::optional<std::string> status{};
        /// Optional content produced by the tool call. Replaces previous content when set.
        std::optional<std::vector<ToolCallContent> > content{};
        /// Optional file locations affected by the tool call.
        std::optional<std::vector<ToolCallLocation> > locations{};
        /// Optional raw input parameters sent to the tool (arbitrary JSON).
        std::optional<RawJson> rawInput{};
        /// Optional raw output returned by the tool (arbitrary JSON).
        std::optional<RawJson> rawOutput{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PermissionToolCallInfo &) const = default;
    };

    /// Parameters of the `session/request_permission` request (agent -> client).
    struct RequestPermissionParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// The tool call that needs permission.
        PermissionToolCallInfo toolCall;
        /// Options to present to the user.
        std::vector<PermissionOption> options{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const RequestPermissionParams &) const = default;
    };

    /// Permission outcome when the prompt turn was cancelled (`outcome: "cancelled"`).
    struct PermissionCancelledOutcome {
        /// Discriminator; always `"cancelled"` on the wire.
        std::string_view outcome{"cancelled"};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PermissionCancelledOutcome &) const = default;
    };

    /// Permission outcome when the user selected an option (`outcome: "selected"`).
    struct PermissionSelectedOutcome {
        /// Discriminator; always `"selected"` on the wire.
        std::string_view outcome{"selected"};
        /// Identifier of the option the user selected.
        std::string optionId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const PermissionSelectedOutcome &) const = default;
    };

    /// Outcome of a `session/request_permission` request.
    ///
    /// Discriminated by `outcome`; see @ref variant_tag<PermissionOutcome>.
    using PermissionOutcome = std::variant<
        PermissionCancelledOutcome,
        PermissionSelectedOutcome,
        Unknown
    >;

    /// Result of the `session/request_permission` request.
    struct RequestPermissionResult {
        /// The user's decision.
        PermissionOutcome outcome;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const RequestPermissionResult &) const = default;
    };

    // --- Elicitation: elicitation/create (Agent -> Client request), elicitation/complete (notification) ---
    // Flattened form of the spec's mode/scope unions so unknown modes are preserved verbatim:
    //   mode "form": `requestedSchema` (restricted JSON Schema) is set
    //   mode "url":  `elicitationId` and `url` are set
    //   scope: `sessionId` (+ optional `toolCallId`) or `requestId`
    /// Parameters of the `elicitation/create` request (agent -> client).
    ///
    /// Flattened form of the spec's mode/scope unions so unknown modes are preserved verbatim:
    /// - mode `"form"`: `requestedSchema` is set;
    /// - mode `"url"`: `elicitationId` and `url` are set;
    /// - scope: `sessionId` (+ optional `toolCallId`) or `requestId`.
    struct CreateElicitationParams {
        /// Elicitation mode: `"form"` (default) or `"url"` (open set).
        std::string mode{"form"};
        /// Message explaining what is being requested.
        std::string message;
        /// Restricted JSON Schema of the requested form; set in form mode.
        std::optional<RawJson> requestedSchema{};
        /// Identifier of the elicitation; required in URL mode.
        std::optional<std::string> elicitationId{};
        /// URL the user should open; required in URL mode.
        std::optional<std::string> url{};
        /// Optional session scope of the elicitation.
        std::optional<std::string> sessionId{};
        /// Optional tool call scope (together with `sessionId`).
        std::optional<std::string> toolCallId{};
        /// Optional request scope: the JSON-RPC request the elicitation relates to.
        std::optional<RequestId> requestId{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CreateElicitationParams &) const = default;
    };

    /// Result of the `elicitation/create` request.
    struct CreateElicitationResult {
        /// User action: `"accept"`, `"decline"` or `"cancel"` (default).
        std::string action{"cancel"}; // "accept", "decline", "cancel"
        /// Submitted form values when `action == "accept"`; optional.
        std::optional<RawJson> content{}; // submitted form values when action == "accept"
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CreateElicitationResult &) const = default;
    };

    /// Parameters of the `elicitation/complete` notification (agent -> client).
    ///
    /// Signals that an out-of-band (URL mode) elicitation has finished.
    struct CompleteElicitationParams {
        /// Identifier of the completed elicitation.
        std::string elicitationId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const CompleteElicitationParams &) const = default;
    };

    // --- Delegated Filesystem Tools (Agent -> Client requests) ---
    /// Parameters of the `fs/read_text_file` request (agent -> client).
    ///
    /// Requires `clientCapabilities.fs.readTextFile`.
    struct FsReadTextFileParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Absolute path of the file to read.
        std::string path;
        /// Optional 1-based line to start reading from.
        std::optional<std::uint32_t> line{};
        /// Optional maximum number of lines to read.
        std::optional<std::uint32_t> limit{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const FsReadTextFileParams &) const = default;
    };

    /// Result of the `fs/read_text_file` request.
    struct FsReadTextFileResult {
        /// The file contents read.
        std::string content;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const FsReadTextFileResult &) const = default;
    };

    /// Parameters of the `fs/write_text_file` request (agent -> client).
    ///
    /// Requires `clientCapabilities.fs.writeTextFile`.
    struct FsWriteTextFileParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Absolute path of the file to write.
        std::string path;
        /// Text to write to the file.
        std::string content;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const FsWriteTextFileParams &) const = default;
    };

    /// Result of the `fs/write_text_file` request (empty object).
    struct FsWriteTextFileResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const FsWriteTextFileResult &) const = default;
    };

    // --- Delegated Terminal Tools (Agent -> Client requests) ---
    /// Parameters of the `terminal/create` request (agent -> client).
    ///
    /// Requires `clientCapabilities.terminal`.
    struct TerminalCreateParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Command to run.
        std::string command;
        /// Command arguments; may be empty.
        std::vector<std::string> args{};
        /// Optional working directory (absolute path).
        std::optional<std::string> cwd{};
        /// Environment variables to set; may be empty.
        std::vector<EnvVariable> env{};
        /// Optional maximum number of output bytes to retain; older output is truncated beyond it.
        std::optional<std::uint64_t> outputByteLimit{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalCreateParams &) const = default;
    };

    /// Result of the `terminal/create` request.
    struct TerminalCreateResult {
        /// Identifier of the created terminal.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalCreateResult &) const = default;
    };

    /// Parameters of the `terminal/output` request (agent -> client).
    struct TerminalOutputParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the terminal, as returned by `terminal/create`.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalOutputParams &) const = default;
    };

    /// Exit status of a terminal's command.
    struct TerminalExitStatus {
        /// Process exit code; absent if terminated by a signal.
        std::optional<int> exitCode{};
        /// Signal that terminated the process; absent on normal exit.
        std::optional<std::string> signal{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalExitStatus &) const = default;
    };

    /// Result of the `terminal/output` request.
    struct TerminalOutputResult {
        /// Output captured so far.
        std::string output;
        /// Whether output was truncated due to `outputByteLimit`. Defaults to `false`.
        bool truncated{false};
        /// Exit status; present only once the command has exited.
        std::optional<TerminalExitStatus> exitStatus{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalOutputResult &) const = default;
    };

    /// Parameters of the `terminal/wait_for_exit` request (agent -> client).
    struct TerminalWaitForExitParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the terminal, as returned by `terminal/create`.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalWaitForExitParams &) const = default;
    };

    /// Result of the `terminal/wait_for_exit` request: the command's exit status.
    struct TerminalWaitForExitResult {
        /// Process exit code; absent if terminated by a signal.
        std::optional<int> exitCode{};
        /// Signal that terminated the process; absent on normal exit.
        std::optional<std::string> signal{};
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalWaitForExitResult &) const = default;
    };

    /// Parameters of the `terminal/kill` request (agent -> client).
    ///
    /// Kills the command but keeps the terminal valid until released.
    struct TerminalKillParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the terminal, as returned by `terminal/create`.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalKillParams &) const = default;
    };

    /// Result of the `terminal/kill` request (empty object).
    struct TerminalKillResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalKillResult &) const = default;
    };

    /// Parameters of the `terminal/release` request (agent -> client).
    ///
    /// Kills the command if still running and frees the terminal.
    struct TerminalReleaseParams {
        /// Identifier of the target session.
        std::string sessionId;
        /// Identifier of the terminal, as returned by `terminal/create`.
        std::string terminalId;
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalReleaseParams &) const = default;
    };

    /// Result of the `terminal/release` request (empty object).
    struct TerminalReleaseResult {
        /// Extension metadata (`_meta`), passed through verbatim; reserved by ACP for custom data.
        std::optional<RawJson> _meta{};

        /// Defaulted member-wise three-way comparison (also provides `==`).
        auto operator<=>(const TerminalReleaseResult &) const = default;
    };

    // --- Tagged-union discriminators (shared by all codecs) ---
    // `untagged` names the alternative selected when the discriminator key is absent (-1: required).
    /// Tagged-union discriminator metadata for a protocol `std::variant`, shared by all codecs.
    ///
    /// Specializations provide `key` (the discriminator JSON field), `ids` (the tag value of each
    /// known alternative, in index order; the trailing @ref Unknown alternative has no id and catches any
    /// other tag value) and `untagged` (the alternative selected when `key` is absent,
    /// or `-1` if the tag is required).
    /// @tparam Variant The `std::variant` type being described.
    template<typename Variant>
    struct variant_tag;

    /// Discriminator metadata for @ref ContentBlock (discriminated by `type`; the tag is required).
    template<>
    struct variant_tag<ContentBlock> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "type";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 5> ids{"text", "image", "audio", "resource_link", "resource"};
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = -1;
    };

    /// Discriminator metadata for @ref ToolCallContent (discriminated by `type`; the tag is required).
    template<>
    struct variant_tag<ToolCallContent> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "type";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 3> ids{"content", "diff", "terminal"};
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = -1;
    };

    /// Discriminator metadata for @ref SessionUpdatePayload (discriminated by `sessionUpdate`; the tag is required).
    template<>
    struct variant_tag<SessionUpdatePayload> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "sessionUpdate";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 14> ids{
            "user_message_chunk", "agent_message_chunk", "agent_thought_chunk", "tool_call",
            "tool_call_update", "plan", "available_commands_update", "current_mode_update",
            "config_option_update", "session_info_update", "usage_update", "notice",
            "compaction_update", "compaction_summary_chunk"
        };
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = -1;
    };

    /// Discriminator metadata for @ref PermissionOutcome (discriminated by `outcome`; the tag is required).
    template<>
    struct variant_tag<PermissionOutcome> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "outcome";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 2> ids{"cancelled", "selected"};
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = -1;
    };

    /// Discriminator metadata for @ref McpServer (discriminated by `type`; a missing tag selects @ref McpServerStdio).
    template<>
    struct variant_tag<McpServer> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "type";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 3> ids{"http", "sse", "stdio"};
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = 2;
    };

    /// Discriminator metadata for @ref AuthMethod (discriminated by `type`; a missing tag selects @ref AuthMethodAgent).
    template<>
    struct variant_tag<AuthMethod> {
        /// JSON field holding the discriminator.
        static constexpr std::string_view key = "type";
        /// Discriminator value of each alternative, in `std::variant` index order.
        static constexpr std::array<std::string_view, 2> ids{"agent", "terminal"};
        /// Index of the alternative chosen when `key` is absent; `-1` means the tag is required).
        static constexpr int untagged = 0;
    };

    // --- Semantic validation of inbound params (beyond JSON shape) ---
    // Returns a human-readable reason when `params` violates an ACP v1 MUST; both base classes reply
    // with -32602 (Invalid params) in that case.
    /// Checks whether `path` is absolute.
    ///
    /// Accepts POSIX paths (`/...`), paths starting with a backslash, and Windows drive paths
    /// (`C:/...` or `C:\\...`).
    /// @param path Path to check.
    /// @return `true` if `path` is absolute; `false` otherwise (including when empty).
    inline bool is_absolute_path(std::string_view path) noexcept {
        if (path.empty()) return false;
        if (path.front() == '/' || path.front() == '\\') return true;
        const auto drive = path.front();
        return path.size() >= 3 && ((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) &&
               path[1] == ':' && (path[2] == '/' || path[2] == '\\');
    }

    /// Internal helpers used by the @ref validate overloads.
    namespace detail {
        /// Checks that a required identifier/string field is non-empty.
        /// @param value Field value.
        /// @param field Field name used in the error message.
        /// @return `"<field> is required"` if `value` is empty, otherwise `std::nullopt`.
        inline std::optional<std::string> require_id(std::string_view value, std::string_view field) {
            if (value.empty()) return std::string(field) + " is required";
            return std::nullopt;
        }

        /// Checks that a path field is absolute (see @ref is_absolute_path).
        /// @param value Field value.
        /// @param field Field name used in the error message.
        /// @return `"<field> must be an absolute path"` if not absolute, otherwise `std::nullopt`.
        inline std::optional<std::string> require_abs(std::string_view value, std::string_view field) {
            if (!is_absolute_path(value)) return std::string(field) + " must be an absolute path";
            return std::nullopt;
        }

        /// Checks that every entry of an optional path list is absolute.
        /// @param values Optional list of paths; absent passes.
        /// @param field Field name used in the error message.
        /// @return The error for the first non-absolute entry, otherwise `std::nullopt`.
        inline std::optional<std::string> require_abs(const std::optional<std::vector<std::string> > &values,
                                                      std::string_view field) {
            if (values) {
                for (const auto &v: *values) {
                    if (auto err = require_abs(v, field)) return err;
                }
            }
            return std::nullopt;
        }

        /// Returns the first error among several check results.
        /// @tparam Checks Types convertible to `std::optional<std::string>`.
        /// @param checks Already-evaluated check results, in priority order.
        /// @return The first engaged result, or `std::nullopt` if all passed.
        template<typename... Checks>
        std::optional<std::string> first_error(Checks &&... checks) {
            std::optional<std::string> out{};
            ((out ? void() : void(out = std::forward<Checks>(checks))), ...);
            return out;
        }
    } // namespace detail

    /// Semantic validation of inbound params beyond JSON shape (default: always valid).
    ///
    /// Overloads for specific params types check ACP v1 MUST rules; the agent/client base classes
    /// reply with `-32602` (Invalid params) when an error is returned.
    /// @tparam T Params type.
    /// @return `std::nullopt`; this generic overload accepts every value.
    template<typename T>
    std::optional<std::string> validate(const T &) {
        return std::nullopt;
    }

    /// Validates @ref AuthenticateParams (checks that `methodId` is non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const AuthenticateParams &p) {
        return detail::require_id(p.methodId, "methodId");
    }

    /// Validates @ref SessionNewParams (checks that `cwd` and every `additionalDirectories` entry are absolute paths).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionNewParams &p) {
        return detail::first_error(detail::require_abs(p.cwd, "cwd"),
                                   detail::require_abs(p.additionalDirectories, "additionalDirectories"));
    }

    /// Validates @ref SessionLoadParams (checks that `sessionId` is non-empty; `cwd` and every `additionalDirectories` entry are absolute paths).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionLoadParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                                   detail::require_abs(p.additionalDirectories, "additionalDirectories"));
    }

    /// Validates @ref SessionResumeParams (checks that `sessionId` is non-empty; `cwd` and every `additionalDirectories` entry are absolute paths).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionResumeParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.cwd, "cwd"),
                                   detail::require_abs(p.additionalDirectories, "additionalDirectories"));
    }

    /// Validates @ref SessionListParams (checks that `cwd`, when present, is an absolute path).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionListParams &p) {
        if (p.cwd) return detail::require_abs(*p.cwd, "cwd");
        return std::nullopt;
    }

    /// Validates @ref SessionDeleteParams (checks that `sessionId` is non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionDeleteParams &p) {
        return detail::require_id(p.sessionId, "sessionId");
    }

    /// Validates @ref SessionCloseParams (checks that `sessionId` is non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionCloseParams &p) {
        return detail::require_id(p.sessionId, "sessionId");
    }

    /// Validates @ref SessionSetModeParams (checks that `sessionId` and `modeId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionSetModeParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.modeId, "modeId"));
    }

    /// Validates @ref SessionSetConfigOptionParams (checks that `sessionId` and `configId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionSetConfigOptionParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.configId, "configId"));
    }

    /// Validates @ref SessionSetModelParams (checks that `sessionId` and `modelId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionSetModelParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.modelId, "modelId"));
    }

    /// Validates @ref SessionPromptParams (checks that `sessionId` is non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const SessionPromptParams &p) {
        return detail::require_id(p.sessionId, "sessionId");
    }

    /// Validates @ref RequestPermissionParams (checks that `sessionId` and `toolCall.toolCallId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const RequestPermissionParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.toolCall.toolCallId, "toolCall.toolCallId"));
    }

    /// Validates @ref CreateElicitationParams (checks that in URL mode (`mode == "url"`), `elicitationId` and `url` are present and non-empty; other modes are accepted).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const CreateElicitationParams &p) {
        if (p.mode == "url") {
            return detail::first_error(detail::require_id(p.elicitationId.value_or(""), "elicitationId"),
                                       detail::require_id(p.url.value_or(""), "url"));
        }
        return std::nullopt;
    }

    /// Validates @ref FsReadTextFileParams (checks that `sessionId` is non-empty and `path` is an absolute path).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const FsReadTextFileParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
    }

    /// Validates @ref FsWriteTextFileParams (checks that `sessionId` is non-empty and `path` is an absolute path).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const FsWriteTextFileParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"), detail::require_abs(p.path, "path"));
    }

    /// Validates @ref TerminalCreateParams (checks that `sessionId` and `command` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const TerminalCreateParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.command, "command"));
    }

    /// Validates @ref TerminalOutputParams (checks that `sessionId` and `terminalId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const TerminalOutputParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.terminalId, "terminalId"));
    }

    /// Validates @ref TerminalWaitForExitParams (checks that `sessionId` and `terminalId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const TerminalWaitForExitParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.terminalId, "terminalId"));
    }

    /// Validates @ref TerminalKillParams (checks that `sessionId` and `terminalId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const TerminalKillParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.terminalId, "terminalId"));
    }

    /// Validates @ref TerminalReleaseParams (checks that `sessionId` and `terminalId` are non-empty).
    /// @param p Params to validate.
    /// @return A human-readable error message if a MUST rule is violated, otherwise `std::nullopt`.
    inline std::optional<std::string> validate(const TerminalReleaseParams &p) {
        return detail::first_error(detail::require_id(p.sessionId, "sessionId"),
                                   detail::require_id(p.terminalId, "terminalId"));
    }
} // namespace buraq::v1
