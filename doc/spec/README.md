[English](./README.md) | [한국어](./README.ko.md)

# zlink Library Specification

This specification defines the public surface of the zlink library. A
conforming implementation MUST provide every function, type, and constant
described herein with the specified semantics.

## Specification Structure

| Section | Path | Description |
|---------|------|-------------|
| **Core Specification** | [core/](./core/README.md) | C library specification (`zlink.h`) |
| **Bindings Specification** | [bindings/](./bindings/README.md) | Language binding contracts and per-language API specs |

## Core Specification (core/)

The core specification defines the C library interface. An implementation
that satisfies every requirement in this section produces a conforming
zlink C library.

| Document | Description |
|----------|-------------|
| [errors.md](./core/errors.md) | Error codes, error strings, and version query |
| [context.md](./core/context.md) | Context creation, termination, and option tuning |
| [message.md](./core/message.md) | Message lifecycle, data access, ownership, and properties |
| [socket/](./core/socket/README.md) | Socket specifications (common + per-type) |
| [monitoring.md](./core/monitoring.md) | Socket monitors, monitor snapshots, and peer inspection |
| [events.md](./core/events.md) | Canonical event catalog and readiness semantics |
| [service/README.md](./core/service/README.md) | Shared service-layer concepts and document split |
| [registry.md](./core/service/registry.md) | Service registry creation, configuration, and clustering |
| [discovery.md](./core/service/discovery.md) | Service discovery, subscription, and peer lookup |
| [spot.md](./core/service/spot.md) | SPOT topic-based PUB/SUB and routed messaging |
| [polling.md](./core/polling.md) | Proxy helpers and capability query |
| [utilities.md](./core/utilities.md) | Timers, threads, stopwatch, and atomics |

## Bindings Specification (bindings/)

The bindings specification defines how the core C contract is projected
into each target language. An implementation that satisfies the cross-language
policy and the per-language spec produces a conforming zlink binding.

| Document | Description |
|----------|-------------|
| [policy](./bindings/README.md) | Cross-language binding contract (POSD, capability matrix, naming, domain objects) |
| [C](./bindings/c/README.md) | C binding specification |
| [C++](./bindings/cpp/README.md) | C++ binding specification |
| [Java](./bindings/java/README.md) | Java binding specification |
| [.NET](./bindings/dotnet/README.md) | .NET binding specification |
| [Node.js](./bindings/node/README.md) | Node.js binding specification |
| [Python](./bindings/python/README.md) | Python binding specification |
| [Go](./bindings/go/README.md) | Go binding specification |
| [Rust](./bindings/rust/README.md) | Rust binding specification |

## Conformance

A conforming implementation:

1. **MUST** implement every function, type, and constant in the core specification
   with the documented signatures and semantics.
2. **MUST** satisfy the guarantees and constraints stated for each API.
3. **MUST NOT** expose internal implementation details through the public surface.
4. Language bindings **MUST** follow the cross-language policy and their
   respective per-language specification.

## Terminology

- **MUST** / **MUST NOT**: Absolute requirements.
- **SHOULD** / **SHOULD NOT**: Strong recommendations; deviation requires justification.
- **MAY**: Optional behavior.
