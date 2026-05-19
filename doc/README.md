English | [한국어](./README.ko.md)

# zlink Documentation

> zlink project documentation navigation

## Directory Purpose

| Directory | Audience | Purpose |
|-----------|----------|---------|
| `spec/` | Binding developers, API reviewers | **Public API contract** — function signatures, return values, error codes, ownership rules. Source of truth: `core/include/zlink.h` |
| `guide/` | Application developers (library users) | **Intent, purpose, usage** — why the API exists, when to use which pattern, practical examples. No internal implementation details |
| `internals/` | Core library maintainers | **Internal architecture** — socket wiring, data flow, thread model, protocol encoding. Diagram-heavy for understanding before reading code |
| `building/` | Build/release engineers | Build, test, packaging instructions |
| `plan/` | Project contributors | Feature roadmap, working specs, migration plans |

**Key rule**: guide does not explain internals. If a guide topic needs
internal context, link to the internals document instead.

---

## Paths by Audience

| Audience | Starting Document | Description |
|----------|-------------------|-------------|
| **Library Users** | [guide/01-overview.md](./guide/01-overview.md) | Developing messaging applications with the zlink API |
| **Binding Users** | [bindings/overview.md](bindings/overview.md) | C++/Java/.NET/Node.js/Python bindings |
| **Library Developers** | [internals/architecture.md](./internals/architecture.md) | Internal architecture and implementation details |
| **Build/Release Engineers** | [building/build-guide.md](./building/build-guide.md) | Building, testing, and packaging |

---

## User Guide (guide/)

### Core
| Document | Description |
|----------|-------------|
| [01-overview.md](./guide/01-overview.md) | zlink overview and getting started |
| [02-core-api.md](./guide/02-core-api.md) | Core C API detailed guide |
| [03-0-socket-patterns.md](./guide/03-0-socket-patterns.md) | Socket patterns overview and selection guide |
| [03-1-pair.md](./guide/03-1-pair.md) | PAIR socket (1:1 bidirectional) |
| [03-2-pubsub.md](./guide/03-2-pubsub.md) | PUB/SUB/XPUB/XSUB publish-subscribe |
| [03-3-dealer.md](./guide/03-3-dealer.md) | DEALER socket (asynchronous requests) |
| [03-4-router.md](./guide/03-4-router.md) | ROUTER socket (ID-based routing) |
| [03-5-stream.md](./guide/03-5-stream.md) | STREAM socket (RAW communication) |
| [04-transports.md](./guide/04-transports.md) | Transport guide (tcp/ipc/inproc/ws/wss/tls) |
| [05-tls-security.md](./guide/05-tls-security.md) | TLS/SSL configuration and security guide |
| [06-monitoring.md](./guide/06-monitoring.md) | Monitoring API usage |

### Services
| Document | Description |
|----------|-------------|
| [07-0-services.md](./guide/07-0-services.md) | Service layer overview |
| [07-1-discovery.md](./guide/07-1-discovery.md) | Service Discovery infrastructure |
| [07-3-spot.md](./guide/07-3-spot.md) | SPOT (location-transparent messaging: topic pub/sub + routed direct delivery) |

### Reference
| Document | Description |
|----------|-------------|
| [08-routing-id.md](./guide/08-routing-id.md) | Routing ID concepts and usage |
| [09-message-api.md](./guide/09-message-api.md) | Message API details |
| [10-performance.md](./guide/10-performance.md) | Performance characteristics and tuning guide |

## Library Specification (spec/)

| Document | Description |
|----------|-------------|
| [spec/README.md](./spec/README.md) | Specification master index |
| [spec/core/README.md](./spec/core/README.md) | Core C library specification |
| [spec/core/socket/](./spec/core/socket/README.md) | Socket specifications (common + per-type) |
| [spec/bindings/README.md](./spec/bindings/README.md) | Cross-language binding policy and per-language specs |

## Bindings Guide (bindings/)

| Document | Description |
|----------|-------------|
| [overview.md](bindings/overview.md) | Common overview and cross-language API alignment |
| [cpp.md](bindings/cpp.md) | C++ binding (header-only RAII) |
| [java.md](bindings/java.md) | Java binding (FFM API, Java 22+) |
| [dotnet.md](bindings/dotnet.md) | .NET binding (LibraryImport, .NET 8+) |
| [node.md](bindings/node.md) | Node.js binding (N-API) |
| [python.md](bindings/python.md) | Python binding (ctypes/CFFI) |

## Internals (internals/)

| Document | Description |
|----------|-------------|
| [architecture.md](./internals/architecture.md) | System architecture overview (5-layer details) |
| [protocol-zmp.md](./internals/protocol-zmp.md) | ZMP v1.0 protocol details |
| [protocol-raw.md](./internals/protocol-raw.md) | RAW (STREAM) protocol details |
| [stream-socket.md](./internals/stream-socket.md) | STREAM socket internals, WS/WSS optimization, runtime defaults |
| [peer-disconnect-rid.md](./internals/peer-disconnect-rid.md) | Peer disconnect by routing id internals |
| [socket-option-defaults.md](./internals/socket-option-defaults.md) | Effective socket option defaults from code |
| [threading-model.md](./internals/threading-model.md) | Threading and concurrency model |
| [services-internals.md](./internals/services-internals.md) | Service layer internal design (overview) |
| [spot-internals.md](./internals/spot-internals.md) | SPOT/SpotNode internal socket wiring and data flow |
| [discovery-internals.md](./internals/discovery-internals.md) | Discovery service internal architecture |
| [registry-internals.md](./internals/registry-internals.md) | Registry service internal architecture |
| [design-decisions.md](./internals/design-decisions.md) | Design decision records |

## Build and Development (building/)

| Document | Description |
|----------|-------------|
| [build-guide.md](./building/build-guide.md) | Build instructions (CMake, per-platform) |
| [cmake-options.md](./building/cmake-options.md) | CMake options reference |
| [packaging.md](./building/packaging.md) | Release and packaging |
| [release-accounts.md](./building/release-accounts.md) | Official distribution accounts/secrets |
| [../core/tests/README.md](../core/tests/README.md) | Test strategy, layout, and lane execution |
| [platforms.md](./building/platforms.md) | Supported platforms and compilers |

## Reference (plan/)

| Document | Description |
|----------|-------------|
| [feature-roadmap.md](plan/feature-roadmap.md) | Feature roadmap |
| [type-segmentation.md](plan/type-segmentation.md) | Discovery type separation plan |

## Working Specs (plan/)

| Document | Description |
|----------|-------------|
| [plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md](./plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md) | Working spec for SpotNode-based routed message during development |
