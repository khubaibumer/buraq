// Consumer smoke test shared by the Conan recipe (`conan create .`) and the vcpkg port CI job.
#include <buraq/acp.hpp>
#include <buraq/transport/stdio.hpp>

#ifdef BURAQ_TEST_GLAZE
#include <buraq/codecs/glaze.hpp>
#endif
#ifdef BURAQ_TEST_NLOHMANN
#include <buraq/codecs/nlohmann.hpp>
#endif

#include <cstdio>
#include <string_view>
#include <variant>

namespace {
    // Unknown keys (`_meta`) must be ignored; an untagged MCP server must decode as the stdio alternative.
    constexpr std::string_view kSessionNew =
            R"({"cwd":"/workspace","mcpServers":[{"name":"fs","command":"mcp-fs","args":[],"env":[]}],"_meta":{"x":1}})";

    template<typename Codec>
    bool round_trip(const char *name) {
        auto params = Codec::template decode<buraq::v1::SessionNewParams>(kSessionNew);
        if (!params || params->mcpServers.size() != 1 ||
            !std::holds_alternative<buraq::v1::McpServerStdio>(params->mcpServers.front())) {
            std::printf("%s: decode failed\n", name);
            return false;
        }
        const auto wire = Codec::encode_request(buraq::RequestId{std::int64_t{1}}, "session/new", *params);
        std::printf("%s: %s\n", name, wire.c_str());
        return wire.find("\"mcp-fs\"") != std::string::npos;
    }
} // namespace

int main() {
    buraq::v1::InitializeParams init{};
    bool ok = init.protocolVersion == buraq::v1::kProtocolVersion;
#ifdef BURAQ_TEST_GLAZE
    ok = round_trip<buraq::GlazeCodec>("glaze") && ok;
#endif
#ifdef BURAQ_TEST_NLOHMANN
    ok = round_trip<buraq::NlohmannCodec>("nlohmann") && ok;
#endif
    std::printf("buraq test_package: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
