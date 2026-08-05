# Framework Common Spec

The documents in this directory describe the Framework's common public
contract. Each document self-contains the inputs, state, normal flow, and
failure/completion conditions its implementation and contract tests need.

## Authoring Standards And Shared Terms

- [Spec writing guide](../../../../../doc/principal/documentation/spec-writing-guide.ko.md)
- [00 Public contract governance](00-public-contract-governance.ko.md)
- [01 Framework messaging glossary](01-glossary.ko.md)

## Base Contract

- [02 Framework overview](02-overview.ko.md)
- [03 Interaction model](03-interaction-model.ko.md)
- [04 Message model](04-message-model.ko.md)
- [05 Async execution policy](05-async-execution-policy.md)
- [06 Framework API](06-framework-api.ko.md)

## Channel And Network

- [07 RouteMesh topology](07-channel-topology.ko.md)
- [08 Channel messaging](08-channel-messaging.ko.md)
- [09 ClientServer Channel](09-client-server-channel.ko.md)
- [10 Network listener identity](10-network-listener-identity.ko.md)

## Object Messaging

- [11 Spot model](11-spot-model.ko.md)
- [12 Spot messaging](12-spot-messaging.ko.md)
- [13 MeshNode](13-mesh-node.ko.md)
- [14 Actor model](14-actor-model.ko.md)
- [15 Spot and Actor membership](15-spot-actor.ko.md)
- [16 Spot address messaging](16-spot-address-messaging.ko.md)
- [17 Stage wrapper on Spot](17-stage-wrapper-on-spot.ko.md)
- [18 Spot/Actor routing](18-object-routing.ko.md)

## STREAM And Sessions

- [19 STREAM server session](19-stream-session.ko.md)
- [20 Session Actor dispatch](20-session-actor-dispatch.ko.md)

## Location Store And Relocation

- [21 Location runtime](21-location-runtime.ko.md) — defines the order in which the Framework uses object location, authority, and the two Stores.
- [22 Location Store provider SPI and the official Redis implementation](22-location-store-redis.ko.md) — defines the atomic key/value and scan contract a provider must implement.
- [23 Relocation Store provider SPI and the official Redis implementation](23-relocation-store-redis.ko.md) — defines the immutable payload storage contract a provider must implement.

## Observability And Termination

- [24 Runtime state and operational diagnostics](24-runtime-monitoring.ko.md) — defines the health, topology status, and structured logs an application reads.
- [25 Runtime metric names and labels](25-runtime-metrics.ko.md) — defines only metric names, units, and bounded labels.
- [26 Message flow tracing](26-message-flow-tracing.ko.md) — defines the phases, outcomes, and trace attributes of a single message.
- [27 Request correlation and causal flow](27-flow-correlation.ko.md) — defines the generation and propagation of the correlation ID and flow ID.
- [28 Host Relocate and Shutdown](28-graceful-drain-handoff.ko.md) — defines the two relocation modes and the shutdown lifecycle.
- [29 Transport liveness](29-transport-liveness.ko.md)
- [31 Failure handling and failover scope](31-failure-failover-policy.ko.md) — defines the automatic-recovery boundary for target reselection, reconnect, creation recovery, and stateful relocation.
- [32 Framework error model](32-framework-error-model.ko.md) — defines the shared `ErrorKind`, Send/Request completion conditions, and the boundary of an application's retry decision.

## Server Exact Interface Per Language

The exact public types, signatures, and async representation each language
uses for the common server contract are owned by the following documents.

- [C++](server/languages/cpp/README.ko.md)
- [.NET](server/languages/dotnet/README.ko.md)
- [Java](server/languages/java/README.ko.md)
- [Kotlin](server/languages/kotlin/README.ko.md)
- [Node.js](server/languages/node/README.ko.md)

## HTTP Client

- [HTTP client spec index](http-client/README.ko.md)
- [12 HTTP client integration contract](http-client/12-http-client.ko.md)
- [Per-language HTTP client contract](http-client/language-interfaces.ko.md)

`10-revision-candidates.ko.md` is not a public contract — it's a document
that manages design candidates for the next revision.

## Stream Connector

- [32 Stream connector](stream-connector/32-stream-connector.ko.md)
- [Per-language Stream connector contract](stream-connector/README.ko.md#언어별-public-api)
