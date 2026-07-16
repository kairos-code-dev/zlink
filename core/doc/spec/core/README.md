[한국어](README.ko.md) | English

[Specification index](../README.md)

# ZLink Core 10.0.0 specification

This index links the Core 10.0.0 public C ABI contract exposed by `zlink.h`. Formal API documents describe only public contracts; they do not describe source directories, socket wiring, or queue structure.

## 1. Common contracts

| Document | Content |
|---|---|
| [Public-contract governance](00-public-contract-governance.md) | Consistency among specification, headers, tests, and packages |
| [Context](01-context.md) | Context creation, shutdown, and configuration |
| [Message](02-message.md) | Message lifecycle, routing IDs, and ownership |
| [Errors](03-errors.md) | Public result enums, errno, and version |
| [Errno map](04-errno-map.md) | Result and errno mappings by API family |
| [Events](05-events.md) | Common event types and readiness meaning |
| [Polling](06-polling.md) | Poll items, pollers, and source support |
| [Monitoring](07-monitoring.md) | Socket and MeshNode monitors and status snapshots |
| [Utilities](08-utilities.md) | Timers, threads, stopwatch, and atomic helpers |

## 2. Socket contracts

| Document | Content |
|---|---|
| [Socket index](socket/README.md) | Common lifecycle, options, send, and receive |
| [PAIR](socket/01-pair.md) | One-to-one connection |
| [PUB](socket/02-pub.md) | Classic fanout publisher |
| [SUB](socket/03-sub.md) | Classic fanout subscriber |
| [XPUB](socket/04-xpub.md) | Subscription-aware publisher |
| [XSUB](socket/05-xsub.md) | Upstream subscription socket |
| [DEALER](socket/06-dealer.md) | Asynchronous request source |
| [ROUTER](socket/07-router.md) | Raw routing-ID router |
| [STREAM](socket/08-stream.md) | Raw TCP or WebSocket session socket |

## 3. Service contracts

| Document | Content |
|---|---|
| [Service index](service/README.md) | Shared service boundaries and document map |
| [MeshNode](service/01-mesh-node.md) | RouteMesh membership, peers, and node/channel messaging |
| [Dispatch](service/02-dispatch.md) | Readiness, claims, receive batches, and reply tokens |
| [Spot](service/03-spot.md) | Direct Spot messaging and Logical Multicast |
| [Actor](service/04-actor.md) | ActorRef, mailboxes, Spot membership, and transfer |
| [STREAM session](service/05-stream-session.md) | Session-to-Actor bindings and transfer barriers |
