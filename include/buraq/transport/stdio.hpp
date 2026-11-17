#pragma once

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