// include/buraq/acp.hpp
/// @file
/// @brief Umbrella header for the buraq ACP v1 SDK.
///
/// Includes the C++20 compatibility shims (detail/compat.hpp), the JSON-RPC 2.0
/// envelope types and codec concept (protocol/jsonrpc.hpp), the ACP v1 schema
/// (protocol/schema_v1.hpp) and the agent/client bases (core/agent_base.hpp,
/// core/client_base.hpp). JSON codecs (buraq/codecs/glaze.hpp,
/// buraq/codecs/nlohmann.hpp) and the stdio transport (buraq/transport/stdio.hpp)
/// are separate opt-in headers and must be included explicitly.
///
/// @namespace buraq
/// @brief Root namespace of the buraq header-only Agent Client Protocol (ACP) v1 SDK.
///
/// @namespace buraq::v1
/// @brief ACP protocol version 1 schema types (requests, responses, notifications).
///
/// @namespace buraq::detail
/// @brief Internal implementation helpers; not part of the stable public API.
///
/// @namespace buraq::transport
/// @brief Transports that move NDJSON-framed JSON-RPC messages between peers.
///
/// @namespace buraq::error_code
/// @brief JSON-RPC 2.0 and ACP v1 numeric error codes.
#pragma once

#include "buraq/detail/compat.hpp"
#include "buraq/protocol/jsonrpc.hpp"
#include "buraq/protocol/schema_v1.hpp"
#include "buraq/core/agent_base.hpp"
#include "buraq/core/client_base.hpp"
