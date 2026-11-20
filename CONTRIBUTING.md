# Contributing to buraq

Thanks for your interest in improving `buraq`! Bug reports, protocol-compatibility findings, and pull requests are all
welcome.

## Ground rules

- **Header-only, zero-dependency core.** Nothing under `include/buraq/` (except `include/buraq/codecs/`) may include a
  third-party header.
- **Codec parity.** Any schema change must be implemented in both `GlazeCodec` and `NlohmannCodec` and covered by
  `tests/dispatch_test.cpp`, which runs every suite against both codecs.
- **Forward compatibility.** Decoders must ignore unknown JSON keys (including ACP `_meta` fields). Never enable strict
  unknown-key errors.
- **Portability invariants:**
    - *MSVC:* never place `if constexpr (requires ...)` checks against `Derived` methods inside generic lambdas; guard
      at the outer scope with concrete `Derived&` parameters.
    - *AppleClang / libc++:* keep `auto operator<=>(const T&) const = default;` on every struct that participates in a
      `std::variant` (`ContentBlock`, `ToolCallContent`, `SessionUpdatePayload`, `PermissionOutcome`, `McpServer`,
      `AuthMethod`).
- **Tagged unions.** Discriminators live in one place, `v1::variant_tag<V>` in `schema_v1.hpp`, and both codecs derive
  from it. Semantic MUSTs (absolute paths, non-empty ids) belong in `v1::validate()`, not in the codecs.
- **Line endings.** Headers under `include/buraq/` and `examples/echo_agent.cpp` use CRLF; keep the existing style of
  any file you edit.
- **Documentation.** Every class, function, and member in `include/buraq/` needs a `///` Doxygen comment above it. CI
  builds the docs with warnings treated as errors
  (`cmake -B build-docs -DBURAQ_BUILD_DOCS=ON -DBURAQ_DOCS_WARN_AS_ERROR=ON && cmake --build build-docs --target buraq_docs`).
- **C++20 baseline.** C++23 features must be gated behind feature-test macros (see `include/buraq/detail/compat.hpp`).

## Development workflow

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Repeat with `-DCMAKE_CXX_STANDARD=23` to exercise the `std::expected` / `std::generator` paths. Tests use an always-on
`CHECK` macro, so they are meaningful in `Release` builds.

### Code coverage

```bash
cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DBURAQ_ENABLE_COVERAGE=ON
cmake --build build-cov --target buraq_coverage   # report: build-cov/coverage/index.html
```

CI fails if line coverage of `include/buraq/` drops below 90%. New code paths need tests.

### Single-header distribution

`single_include/` is generated. After touching any header under `include/`, regenerate and commit it:

```bash
python3 scripts/amalgamate.py
```

CI runs `python3 scripts/amalgamate.py --check` and fails on drift.

### Official SDK wire check

```bash
npm install --no-save @agentclientprotocol/sdk
node scripts/verify_sdk.mjs ./build/buraq_echo_agent
```

### Official conformance kit

```bash
uvx --from git+https://github.com/agentclientprotocol/acp-tck@v0.4.0 acp-tck -- "$PWD/build/buraq_echo_agent"
```

All MANDATORY requirements must keep passing.

### Packaging (vcpkg / Conan)

```bash
# vcpkg: build the in-repo port from your working tree
python3 scripts/vcpkg_port.py overlay /tmp/buraq-overlay
vcpkg install "buraq[glaze,nlohmann-json]" --overlay-ports=/tmp/buraq-overlay

# Conan 2: package and run test_package
conan create . -s compiler.cppstd=23 -o "buraq/*:with_glaze=True" -o "buraq/*:with_nlohmann_json=True"
```

When you add a public header, add it to `target_sources` in `CMakeLists.txt`. The Conan recipe packages every
`include/**/*.hpp`.

## Releasing

1. Bump `project(buraq VERSION ...)` in `CMakeLists.txt`, move `CHANGELOG.md` `[Unreleased]` entries under the new
   version and date, and commit.
2. Run `gh workflow run release.yml -f version=X.Y.Z`. It runs the full CI, tags `vX.Y.Z`, and publishes the release
   with `acp.hpp`.
3. Run `python3 scripts/vcpkg_port.py pin` to set the port's version and SHA512 for the new tag (the release job summary
   also prints the hash), then commit.

## Pull requests

1. Fork and branch from `main`.
2. Keep changes focused; add or update tests for behaviour changes.
3. Make sure the CI matrix (GCC, Clang, AppleClang, MSVC × C++20/23), the SDK compliance and ACP TCK jobs, the
   single-header check, the vcpkg / Conan packaging jobs, and the Docs build pass.
4. Update `README.md` (method coverage table, examples) and `CHANGELOG.md` (`[Unreleased]`) when the public API changes.

By submitting a contribution you agree that it is licensed under the [Apache License 2.0](LICENSE).
