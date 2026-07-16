[한국어](README.ko.md) | English

# ZLink 10.0.0 public specification

This directory defines the ZLink 10.0.0 public API contract. Its audience is Core and bindings implementers and public-contract reviewers. Formal documents linked from this index are authoritative for function signatures, returns, errors, ownership, and thread safety.

## 1. Document structure

| Area | Document | Description |
|---|---|---|
| Core C ABI | [Core specification](core/README.md) | C functions, types, enums, and runtime behavior |
| Bindings | [Bindings specification](../../../bindings/doc/spec/README.md) | Language-specific public projections of the Core contract |

Formal specifications describe only the current 10.0.0 contract. Guides own purpose and examples, while internals own actual internal structure after implementation is complete. Contract reviewers navigate from this index and the public header.

## 2. Main Core documents

| Document | Public contract |
|---|---|
| [Contract governance](core/00-public-contract-governance.md) | Consistency among specification, headers, tests, and packages |
| [Context](core/context.md) | Context lifecycle and options |
| [Message](core/message.md) | Message and routing-ID storage and ownership |
| [Socket](core/socket/README.md) | Generic socket types and send/receive behavior |
| [Service](core/service/README.md) | MeshNode, Spot, Actor, and STREAM-session behavior |
| [Polling](core/polling.md) | Poll items, pollers, and readiness |
| [Monitoring](core/monitoring.md) | Socket and MeshNode monitors and snapshots |
| [Events](core/events.md) | Public events and state-transition meaning |
| [Errors](core/errors.md) | Result enums, errno, and the 10.0.0 version ABI |
| [Errno map](core/errno-map.md) | Per-function result and errno mappings |
| [Utilities](core/utilities.md) | Timers, threads, stopwatch, and atomic utilities |

## 3. Conformance

A conforming implementation provides every function, type, constant, and behavior in the formal documents. A mismatch among public headers, exported symbols, contract tests, bindings, installed packages, and the formal specification is nonconforming. Internal implementation details are not exposed as public contracts, and a language-specific API cannot reduce the shared contract.
