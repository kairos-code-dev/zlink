[한국어](README.ko.md) | English

[Specification index](../README.md)

# ZLink Core 10.0.0 specification

This index links the Core 10.0.0 public C ABI contract exposed by `zlink.h`. Formal API documents describe only public contracts; they do not describe source directories, socket wiring, or queue structure.

## 1. Common contracts

| Document | Content |
|---|---|
| [Public-contract governance](00-public-contract-governance.md) | Consistency among specification, headers, tests, and packages |
| [Context](context.md) | Context creation, shutdown, and configuration |
| [Message](message.md) | Message lifecycle, routing IDs, and ownership |
| [Errors](errors.md) | Public result enums, errno, and version |
| [Errno map](errno-map.md) | Result and errno mappings by API family |
| [Events](events.md) | Common event types and readiness meaning |
| [Polling](polling.md) | Poll items, pollers, and source support |
| [Monitoring](monitoring.md) | Socket and MeshNode monitors and status snapshots |
| [Utilities](utilities.md) | Timers, threads, stopwatch, and atomic helpers |

## 2. Socket contracts

| Document | Content |
|---|---|
| [Socket index](socket/README.md) | Common lifecycle, options, send, and receive |
| [PAIR](socket/pair.md) | One-to-one connection |
| [PUB](socket/pub.md) | Classic fanout publisher |
| [SUB](socket/sub.md) | Classic fanout subscriber |
| [XPUB](socket/xpub.md) | Subscription-aware publisher |
| [XSUB](socket/xsub.md) | Upstream subscription socket |
| [DEALER](socket/dealer.md) | Asynchronous request source |
| [ROUTER](socket/router.md) | Raw routing-ID router |
| [STREAM](socket/stream.md) | Raw TCP or WebSocket session socket |

## 3. Service contracts

| Document | Content |
|---|---|
| [Service index](service/README.md) | Shared service boundaries and document map |
| [MeshNode](service/mesh-node.md) | RouteMesh membership, peers, and node/channel messaging |
| [Dispatch](service/dispatch.md) | Readiness, claims, receive batches, and reply tokens |
| [Spot](service/spot.md) | Direct Spot messaging and Logical Multicast |
| [Actor](service/actor.md) | ActorRef, mailboxes, Spot membership, and transfer |
| [STREAM session](service/stream-session.md) | Session-to-Actor bindings and transfer barriers |
