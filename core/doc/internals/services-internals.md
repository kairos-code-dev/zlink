[English](services-internals.md) | [한국어](services-internals.ko.md)

# Service Layer Internal Design

This document helps core maintainers navigate the actual internal structure of
the 10.0.0 service layer (MeshNode, Spot, Actor and STREAM session). The
public contract is owned by the formal specs under
[`doc/spec/core/service/`](../spec/core/service/README.md); this document only
describes the current source structure that implements that contract.

## 1. Source layout and responsibility boundaries

| Location | Responsibility |
|---|---|
| `src/runtime/services/mesh/mesh_runtime.{hpp,cpp}` | Object model and process-local state machine: `mesh_node_t`, owner mailboxes, ready index, claims, budgets, monitor queue, handle registry |
| `src/runtime/services/mesh/mesh_wire.{hpp,cpp}` | Remote wire: node-owned raw ROUTER, ingress thread, envelope codec, HELLO admission, remote messaging and the transfer data plane |
| `src/api/mesh/mesh_node_api.cpp` | Lifecycle, membership, peer intents, options, status and query C API |
| `src/api/mesh/mesh_messaging_api.cpp` | Node/channel/Spot direct send and request, Logical Multicast publish |
| `src/api/mesh/mesh_dispatch_api.cpp` | Ready handler, drain, ready/receive batches, claims, reply tokens |
| `src/api/mesh/mesh_actor_api.cpp` | Actor creation, lookup, destroy, join and messaging |
| `src/api/mesh/mesh_transfer_api.cpp` | Actor transfer prepare/commit/activate/abort and the fence |
| `src/api/mesh/mesh_monitor_api.cpp` | MeshNode monitor open/handler/recv/status/close |
| `src/api/mesh/mesh_stream_session_api.cpp` | STREAM session service and Actor bindings |
| `src/api/mesh/mesh_api.cpp` | The seam through which cross-cutting concerns (poller, timer) enter mesh |

Layering rule: `api/mesh/*` owns public signature validation and result
mapping, and delegates every state change into `mesh_runtime`/`mesh_wire`
functions. The raw socket layer (`runtime/sockets/`) knows nothing about mesh.
The only extension mesh asks of the raw ROUTER is the non-consuming write
probe `routed_target_writable()` used by the NODROP atomic reserve.

## 2. Object model

```
zlink_ctx
 └─ mesh_node_t (one MeshName per process, validated by the immortal registry)
     ├─ owns: one raw ROUTER socket (one bind, all peer pipes)
     ├─ one ingress thread (recv + socket monitor drain)
     ├─ peers: vector<peer_state_t>            — single table of intents and admitted peers
     ├─ channels: map<ChannelName, weight>     — names frozen after start, weight mutable
     ├─ owners: map<owner_id_t, owner_state_t> — Node, Spot and Actor mailboxes
     │    └─ domains[2]: application / infrastructure mailbox
     ├─ ready: set<(owner, domain)>            — level-triggered ready index
     ├─ reply_routes: map<serial, reply_route_t> — one-shot reply seal store
     ├─ operations: map<op_low, pending_operation_t>
     ├─ transfers: map<serial, transfer_state_t>
     ├─ spots / actors: logical registries (facades are thin handles)
     └─ monitor: one monitor_state_t (bounded queue + counters)
```

- Handle validity is decided by a global immortal registry (`registry ()`).
  Its storage is intentionally leaked to avoid static destruction ordering
  problems (the same pattern as the request timeout scheduler).
- `owner_id_t` is (kind, key, generation). A different Spot/Actor generation is
  a different mailbox; access through a stale generation ends as a missing
  mailbox.
- Spot facades, publishers, the monitor and STREAM session services all count
  as strong child references of the node: `zlink_mesh_node_destroy` refuses
  with `EBUSY` until they are closed.

## 3. Mailboxes, the ready index and claims

The single admission gate for messages is `admit_record()`.

- The application domain is bounded by the message/byte budgets; on overflow a
  non-blocking call fails with `EAGAIN` and a blocking call waits on the
  condition variable until the deadline and fails with `ETIMEDOUT`. The
  rejection emits the `BACKPRESSURED` monitor event from the same function.
- The infrastructure domain (completions, transfer control, SEND_READY) is
  bounded by the outstanding-operation set, so no budget applies and it always
  makes progress.
- While a transfer fence holds an owner, application admission fails with
  `EAGAIN` until commit or abort resolves it.

The ready index is a set of (owner, domain) pairs and is level-triggered.
`drain_ready` moves ready entries into batch claims; `claim_recv_batch` moves
mailbox records into the receive batch and returns budget as it does so. A
claim release re-arms through `signal_ready` only when records remain in the
mailbox (the decision is taken under the lock, the signal is sent after
releasing it).

A claim's identity is (node generation, owner, domain, serial); the
serial-to-owner reverse map lives in a global side table (`g_claim_keys`).
That table is what makes releasing a claim after node destruction a safe
no-op.

There are three wakeup paths with mutual-exclusion rules:

1. Blocking `drain_ready` — the node condition variable.
2. The ready handler — `notify_consumer_locked` invokes the registered
   wakeup-only callback; re-entrant deregistration is `EDEADLK`.
3. Poller integration — the node's built-in `signaler_t` registers as
   `poller_subject_mesh_node`. POLLIN is the level signal for "ready index is
   non-empty": drain consumes the signal byte and re-arms while claimable work
   remains. Handler and poller registration are mutually exclusive (`EBUSY`).

## 4. Reply tokens and completions

Registering a request creates a one-shot route in `reply_routes[serial]` and
exposes only the serial through a 32-byte sealed token. Consumption rules:

- A successful reply consumes the route; a second reply is `EALREADY`.
- If the requester timed out first, the reply consumes the token and is
  silently discarded (no second completion is produced).
- With the node STOPPED, replies fail with `ESHUTDOWN`. They stay legal during
  DRAINING — a held claim turn must be able to answer to the end.
- An Actor join token has its kind sealed in: passing it to the generic reply
  is `EINVAL` and does not consume it.

Completions always land in the requester owner's infrastructure mailbox.
Timeout completions are produced by the global timeout scheduler (an immortal
singleton with its own thread) calling `complete_operation` at the deadline.

## 5. Wire: ROUTER, the ingress thread and the envelope

- The node's raw ROUTER is thread-safe, so sends happen directly on
  application threads while one dedicated ingress thread owns receive.
- The ingress thread also drains the socket monitor. `CONNECTION_READY`
  (carrying rid and remote_addr) matches outbound intents and triggers the
  HELLO. Peer loss flows through `handle_peer_down` into a state transition
  plus the `PEER_CLOSED` event.
- Envelope v1: `'Z' 'M' | ver | type | flags`. Request/reply kinds append a
  correlation u64; replies append the terminal result and errno; channel kinds
  append the channel name. Application metadata travels in a separate frame
  (flag 0x01) and must pass `validate_metadata` before any mailbox admission;
  a failure drops the complete message and emits `PROTOCOL_ERROR`.
- Wire types: HELLO/ADMIT/REJECT/UPDATE (control), node/channel send/request
  and REPLY, SPOT_SEND=21 and SPOT_REQUEST=22 (addressed by target spot rid +
  generation), MULTICAST=23 (channel, topic, source spot rid), the ACTOR
  family 24–29 (SEND/REQUEST/LOOKUP/DESTROY/JOIN/LEFT) and the TRANSFER family
  30–33 (READY/DATA/ACK/REPLY_RELAY).
- Admission validation (`validate_admission_locked`) checks MeshName
  (`EEXIST`), trust profile (`EACCES`) and expected RID / stale generation
  (`ESTALE`). Rejections are observable through the REJECT frame and the
  `PEER_REJECTED` event on both sides. A higher lifecycle generation replaces
  the previously admitted entry immediately; that replacement is where the
  `PEER_DRAINING` event is emitted. A peer observed inbound (with no local
  intent) has source `DISCOVERY`; a later manual intent for the same endpoint
  merges into `MIXED`.
- A remote request registers the operation with correlation = op.low; the
  responder creates a remote-origin reply route (sealing the origin rid and
  correlation) so that `zlink_mesh_reply` answers over the wire with a REPLY,
  and the requester's ingress closes the pending operation with a completion.

## 6. Logical Multicast and NODROP

Publish completes snapshot → check → commit within one call.

1. Under the node mutex, take the local subscription matches and the snapshot
   of admitted positive-weight channel-member peers.
2. With NODROP (default 1), serialize the node's outbound wire under
   `wire_send_mutex` and pre-check the local mailbox budgets plus
   `routed_target_writable()` on every remote pipe. If any target cannot
   accept, nothing commits (all-or-none). Non-blocking fails with `EAGAIN`;
   blocking retries the reserve until SNDTIMEO and fails with `ETIMEDOUT`.
3. Commit is the local fanout (shared `zlink_msg_copy` refcounts) plus exactly
   one wire submit per peer. A receiving node fans out only to its own local
   subscription matches and has no re-propagation path (structural no-relay).

The publish detail and the `MULTICAST_COMMITTED`/`MULTICAST_DROPPED` events
report the snapshot/admitted/dropped counts verbatim.

## 7. Actors and the transfer fence

Actor state lives in the same owner table as its mailboxes. The single
membership commit point for join is the source-side wire reply handling
(`actor_apply_remote_join_reply`): epoch+1, recording `spot_node_rid` and the
ACTOR_LEFT notification to the previous remote spot all happen there.

Transfers are tracked by `transfer_state_t` (serial, role, phase, frozen
snapshot, staged map, ack high-water). Key decisions:

- The 64-byte token = node ptr + serial + magic (the same sealing pattern as
  reply tokens).
- The fence is enforced at three points: `admit_record` (application
  `EAGAIN`), `take_claim` (`EBUSY`) and `drain_ready` (skips the fenced app
  lane).
- The data plane is stop-and-wait with one record per ACK. The source resends
  its frozen mailbox snapshot in order; the target accumulates into the staged
  map and moves it into the app mailbox at activate.
- Replies for ACTOR_REQUESTs caught mid-transfer travel through a resealed
  route on the target via REPLY_RELAY back through the source to the original
  requester. After the source commits, that route is removed.
- Source commit erases the actors entry but keeps the owners entry so
  remaining infra records stay drainable.

## 8. Monitor

One monitor per node (bounded queue). `emit_monitor_event` applies the mask
and enqueues; on overflow it aggregates high-frequency kinds
(MESSAGE_SUBMITTED, BACKPRESSURED) while preferring to keep peer state,
protocol error and lifecycle events. Counters accumulate independently of
event consumption and the status query is an atomic snapshot. Handler and recv
are the single consumer of the same queue; re-entrant deregistration inside
the handler is `EDEADLK`. `CLAIM_REVOKED` is emitted by the shutdown revoke
loop after it drops the node lock.

## 9. Locking rules and thread inventory

| Thread | Owner | Role |
|---|---|---|
| Application threads | caller | all public APIs, direct wire sends |
| Ingress thread | 1 per node | ROUTER recv, socket monitor drain, remote record admission, completion delivery |
| Timeout scheduler | 1 per process (immortal) | timeout completions at operation deadlines |

- The primary lock is the single `node->mutex`. The monitor has its own mutex;
  `emit_monitor_event` must be called without the node mutex held (internally
  it briefly takes node mutex, then monitor mutex, in that order).
- `wire_send_mutex` serializes all of a node's outbound wire during a NODROP
  atomic reserve. When held together with the node mutex, the node mutex is
  always the outer lock.
- `admit_record` takes the node mutex itself — callers must not hold it when
  calling (the unlock-before-emit in the transfer code is one example).

## 10. STREAM session boundary

A STREAM session service attaches 1:1:1 (service : raw STREAM socket : node)
and owns only the Actor binding CAS (generation) and session-to-Actor
delivery. See [`stream-socket.md`](stream-socket.md) and the formal spec
[`05-stream-session.md`](../spec/core/service/05-stream-session.md). The
bounded post-barrier allowance of the Actor move barrier is enforced by the
participant entry of the transfer state.
