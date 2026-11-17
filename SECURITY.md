# Security Policy

## Supported versions

`buraq` is pre-1.0. Security fixes are applied to the latest released `0.x` version and `main`.

| Version | Supported |
|---|---|
| latest `0.x` release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

**Please do not open public issues for security problems.**

Report vulnerabilities privately through GitHub's [private vulnerability reporting](https://github.com/khubaibumer/buraq/security/advisories/new). Include:

- affected version / commit,
- the codec (`GlazeCodec`, `NlohmannCodec`, or custom) and compiler,
- a minimal reproducing JSON-RPC frame or program, and
- the observed impact (crash, memory-safety issue, protocol confusion, etc.).

You should receive an acknowledgement within 7 days. Once a fix is available, a GitHub Security Advisory will be published crediting the reporter (unless you prefer to remain anonymous).

## Scope

In scope: parsing and dispatch of untrusted JSON-RPC frames in `include/buraq/` and `single_include/` (for example memory-safety bugs, unbounded resource use, or request-ID confusion).

Out of scope: vulnerabilities in third-party JSON libraries (report those upstream to Glaze or nlohmann/json), and application-level policy decisions. In particular, `buraq` relays `fs/*` and `terminal/*` requests but does **not** sandbox them; clients are responsible for validating paths and commands, and agents should only issue them when the client advertised the capability (see `AgentBase::client_capabilities()`).
