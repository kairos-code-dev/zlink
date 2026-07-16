[한국어](events.ko.md) | English

[Specification index](../README.md) · [Core index](README.md) · [Monitoring](monitoring.md) · [Polling](polling.md) · [Dispatch](service/dispatch.md)

# Event and readiness catalog

This document organizes the public event families and readiness meanings in ZLink Core 10.0.0. Its audience is developers who project monitors, pollers, and service dispatch into bindings. The linked owner document defines each event structure and API; this document defines boundaries among families.

## 1. Event families

| Family | Source | Delivery API | Meaning |
|---|---|---|---|
| socket monitor | raw socket monitor handle | handler or receive | Bind, connect, handshake, disconnect, protocol, and close |
| MeshNode monitor | MeshNode monitor handle | handler or receive | Lifecycle, peer, multicast, backpressure, operation, and claim state |
| poller readiness | socket, FD, timer, or MeshNode | `zlink_poll` or poller wait | Draining or retrying submit is currently worthwhile |
| service ready | MeshNode ready index | ready handler, or ready batch after `POLLIN` | An application or infrastructure owner claim can be acquired |
| timer fire | timer handle | handler or timer receive | An accumulated fire count is available |

Monitor events are observation records, while readiness is level-triggered state indicating possible current work. One readiness notification is not assumed to correspond to one application message.

## 2. Raw socket lifecycle

Raw socket monitors record endpoint bind and listen, outgoing connect, accept, handshake success or failure, disconnect, protocol error, and close. Disconnect reasons distinguish transport error, handshake failure, Context termination, and unknown. Raw events contain no MeshName, ChannelName, or service owner.

## 3. MeshNode lifecycle and peers

The MeshNode monitor records these state transitions:

```text
CREATED -> STARTED -> PARTIAL_READY <-> READY -> DRAINING -> STOPPED
                         |               |
                         +---- ERROR <---+
```

Peer events use RID together with lifecycle generation. An endpoint string alone does not define peer identity. Admission rejection distinguishes MeshName, expected RID, generation, and trust failures through result and errno.

## 4. Logical Multicast and backpressure

A Logical Multicast event records aggregate snapshot, admitted, and dropped target counts for one publish. It distinguishes local Spot matches from remote targets and contains neither topics nor payload. A successful default-NODROP publish has zero dropped targets.

A backpressure event means that queue or reservation capacity prevented submission. Send-ready and `POLLOUT` mean that retry is worthwhile; they do not guarantee success of the next submit.

## 5. Service readiness and claims

The MeshNode ready callback reports only a readable domain mask and no payload. Every ready-batch record owns exactly one application or infrastructure domain and one claim. Two domains of the same owner use distinct ready records, separating an application turn from completion progress.

When work remains after claim release, Core sets the ready index again. The single-consumer receive-mode rule prevents another consumer from draining immediately after readiness observation, preserving level-triggered behavior without lost wakeups.

## 6. Ordering and overflow

One source queue preserves the order in which Core commits events. There is no global wall-clock order across peer I/O threads or between raw-socket and MeshNode monitors.

Monitor-queue overflow is reflected in status counters while lifecycle, peer, and protocol events are prioritized. A service ready index never drops payload; remaining work is signaled again when the consumer releases its claim.
