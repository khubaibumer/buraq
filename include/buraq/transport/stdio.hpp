/// @file
/// @brief NDJSON stdio transport: newline-delimited JSON-RPC frames over streams.
///
/// Opt-in header (not included by buraq/acp.hpp).
#pragma once

#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace buraq::transport {

// Sink callable compatible with AgentBase::feed(..., sink)
/// Outbound sink writing each frame as one NDJSON line to a stream.
///
/// Callable compatible with `AgentBase::feed(..., sink)`.
struct StdoutSink {
  /// Destination stream (defaults to std::cout).
  std::ostream& out{std::cout};

  /// Writes @p bytes, appends a newline if not already newline-terminated, and flushes.
  /// @param bytes Encoded frame.
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
/// Blocking NDJSON read loop for standalone CLI ACP agents.
///
/// Disables stdio synchronization, unties @p in, then reads lines until EOF.
/// Empty lines are skipped and a trailing `\r` is stripped; each line is passed to
/// `agent.feed()` with a StdoutSink on @p out. Frame errors are logged to std::cerr.
/// @tparam Agent Type providing `feed(std::span<const std::byte>, Sink&&)`.
/// @param agent The agent to feed.
/// @param in Input stream (defaults to std::cin).
/// @param out Output stream (defaults to std::cout).
/// @return 0 when input is exhausted.
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