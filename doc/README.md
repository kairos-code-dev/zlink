English | [한국어](README.ko.md)

# zlink Documentation

> zlink project documentation navigation

---

## Paths by Audience

| Audience | Starting Document | Description |
|----------|-------------------|-------------|
| **Library Users** | [guide/01-overview.md](guide/01-overview.md) | Developing messaging applications with the zlink API |
| **Binding Users** | [bindings/overview.md](bindings/overview.md) | C++/Java/.NET/Node.js/Python bindings |
| **Library Developers** | [internals/architecture.md](internals/architecture.md) | Internal architecture and implementation details |
| **Build/Release Engineers** | [building/build-guide.md](building/build-guide.md) | Building, testing, and packaging |

---

## User Guide (guide/)

### Core
| Document | Description |
|----------|-------------|
| [01-overview.md](guide/01-overview.md) | zlink overview and getting started |
| [02-core-api.md](guide/02-core-api.md) | Core C API detailed guide |
| [03-0-socket-patterns.md](guide/03-0-socket-patterns.md) | Socket patterns overview and selection guide |
| [03-1-pair.md](guide/03-1-pair.md) | PAIR socket (1:1 bidirectional) |
| [03-2-pubsub.md](guide/03-2-pubsub.md) | PUB/SUB/XPUB/XSUB publish-subscribe |
| [03-3-dealer.md](guide/03-3-dealer.md) | DEALER socket (asynchronous requests) |
| [03-4-router.md](guide/03-4-router.md) | ROUTER socket (ID-based routing) |
| [03-5-stream.md](guide/03-5-stream.md) | STREAM socket (RAW communication) |
| [04-transports.md](guide/04-transports.md) | Transport guide (tcp/ipc/inproc/ws/wss/tls) |
| [05-tls-security.md](guide/05-tls-security.md) | TLS/SSL configuration and security guide |
| [06-monitoring.md](guide/06-monitoring.md) | Monitoring API usage |

### Services
| Document | Description |
|----------|-------------|
| [07-0-services.md](guide/07-0-services.md) | Service layer overview |
| [07-1-discovery.md](guide/07-1-discovery.md) | Service Discovery infrastructure |
| [07-2-gateway.md](guide/07-2-gateway.md) | Gateway service (location-transparent request/response) |
| [07-3-spot.md](guide/07-3-spot.md) | SPOT topic PUB/SUB (location-transparent publish/subscribe) |

### Reference
| Document | Description |
|----------|-------------|
| [08-routing-id.md](guide/08-routing-id.md) | Routing ID concepts and usage |
| [09-message-api.md](guide/09-message-api.md) | Message API details |
| [10-performance.md](guide/10-performance.md) | Performance characteristics and tuning guide |

## API Reference (api/)

| Document | Description |
|----------|-------------|
| [README.md](api/README.md) | API reference index |
| [errors.md](api/errors.md) | Error handling and versioning |
| [context.md](api/context.md) | Context API |
| [message.md](api/message.md) | Message API |
| [socket.md](api/socket.md) | Socket API |
| [monitoring.md](api/monitoring.md) | Monitoring and peer information |
| [registry.md](api/registry.md) | Registry API |
| [discovery.md](api/discovery.md) | Discovery API |
| [gateway.md](api/gateway.md) | Gateway API |
| [receiver.md](api/receiver.md) | Receiver API |
| [spot.md](api/spot.md) | SPOT PUB/SUB API |
| [polling.md](api/polling.md) | Polling and Proxy |
| [utilities.md](api/utilities.md) | Utilities (timer, thread, etc.) |

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
| [architecture.md](internals/architecture.md) | System architecture overview (5-layer details) |
| [protocol-zmp.md](internals/protocol-zmp.md) | ZMP v2.0 protocol details |
| [protocol-raw.md](internals/protocol-raw.md) | RAW (STREAM) protocol details |
| [stream-socket.md](internals/stream-socket.md) | STREAM socket WS/WSS optimization |
| [threading-model.md](internals/threading-model.md) | Threading and concurrency model |
| [services-internals.md](internals/services-internals.md) | Service layer internal design |
| [design-decisions.md](internals/design-decisions.md) | Design decision records |

## Build and Development (building/)

| Document | Description |
|----------|-------------|
| [build-guide.md](building/build-guide.md) | Build instructions (CMake, per-platform) |
| [cmake-options.md](building/cmake-options.md) | CMake options reference |
| [packaging.md](building/packaging.md) | Release and packaging |
| [release-accounts.md](building/release-accounts.md) | Official distribution accounts/secrets |
| [testing.md](building/testing.md) | Test strategy and execution |
| [platforms.md](building/platforms.md) | Supported platforms and compilers |

## Reference (plan/)

| Document | Description |
|----------|-------------|
| [feature-roadmap.md](plan/feature-roadmap.md) | Feature roadmap |
| [type-segmentation.md](plan/type-segmentation.md) | Discovery type separation plan |
