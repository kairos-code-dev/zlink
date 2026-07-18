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
| `src/runtime/services/mesh/mesh_wire*.{hpp,cpp}` | Remote wire in four modules — `mesh_wire_codec` (the wire format decision), `mesh_wire_admission` (the peer admission state machine), `mesh_wire_ingress` (routing received frames into the services plus the ingress thread) and `mesh_wire` (transport lifecycle and outbound submits). Shared declarations live in `mesh_wire_internal.hpp` |
| `src/api/mesh/mesh_node_api.cpp` | Lifecycle, membership, peer intents, options, status and query C API |
| `src/api/mesh/mesh_messaging_api.cpp` | Node/channel/Spot direct send and request, Logical Multicast publish |
| `src/api/mesh/mesh_dispatch_api.cpp` | Ready handler, drain, ready/receive batches, claims, reply tokens |
| `src/api/mesh/mesh_actor_api.cpp` | Actor creation, lookup, destroy, join and messaging |
| `src/api/mesh/mesh_transfer_api.cpp` | Actor transfer prepare/commit/activate/abort and the fence |
| `src/api/mesh/mesh_monitor_api.cpp` | MeshNode monitor open/handler/recv/status/close |
| `src/api/mesh/mesh_stream_session_api.cpp` | STREAM session service and Actor bindings |
| `src/api/mesh/mesh_api.cpp` | The seam through which cross-cutting concerns (poller, timer) enter mesh |

Layering rule: `api/mesh/*` owns public signature validation and result
mapping, and delegates state changes into `mesh_runtime`/`mesh_wire`
functions. The exception is the cross-cutting seam `mesh_api.cpp`: the Spot
timer registry (including cancellation) and the turn-admission state
(`timer_turn_active`, `timer_count`) are owned and mutated by that seam
directly — the timer machinery only provides hooks, keeping the
Spot-coupling knowledge in one place on the mesh side. The raw socket layer (`runtime/sockets/`) knows nothing about mesh.
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
- Public entry points hold the node's lifetime through a registry pin (RAII
  `mesh_node_pin_t`): destroy removes the handle from the registry and then
  waits for the pin count to drain before releasing the storage. The
  shutdown-only pin rejects a claimed destroy as the §11 re-entry
  (`EDEADLK`), and the destroy claim rejects a second destroy with
  `ESTALE`. Blocking wait paths wake on destroy's forced-stop notification
  and return through their state checks, so pins drain in bounded time.
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

A claim's identity is (node generation, owner, domain, serial); serials come
from **one process-wide atomic counter**, so they never collide across
MeshNodes. The serial-to-owner reverse map lives in an immortal side table,
which is what makes releasing a claim after node destruction a safe no-op.

There are three wakeup paths with mutual-exclusion rules:

1. Blocking `drain_ready` — the node condition variable.
2. The ready handler — `notify_consumer_locked` invokes the registered
   wakeup-only callback; re-entrant deregistration is `EDEADLK`.
3. Poller integration — the node's built-in `signaler_t` registers as
   `poller_subject_mesh_node`. POLLIN is the level signal for "ready index is
   non-empty": drain consumes the signal byte and re-arms while claimable work
   remains. Handler and poller registration are mutually exclusive (`EBUSY`).

## 4. Request transactions, reply tokens and completions

Before request delivery, `operation_submission_t` prepares the operation, its
one-shot route in `reply_routes[serial]`, and the storage needed by the timeout
task as one transaction. If preparation fails, its destructor removes the
operation and route and cancels the timeout. A submit path calls `commit()`
only after it has delivered the request to the target mailbox or wire. A
request reported as failed therefore cannot have reached its target or remain
as an operation that the caller cannot identify.

The 32-byte sealed reply token exposes only the route serial. Consumption
rules:

- A successful reply consumes the route; a second reply is `EALREADY`.
- If the requester timed out first, the reply consumes the token and is
  silently discarded (no second completion is produced).
- With the node STOPPED, replies fail with `ESHUTDOWN`. They stay legal during
  DRAINING — a held claim turn must be able to answer to the end.
- An Actor join token has its kind sealed in: passing it to the generic reply
  is `EINVAL` and does not consume it.

Completions always land in the requester owner's infrastructure mailbox. Each
operation allocates an empty completion record and list node when it is
registered. A terminal path prepares the payload in that record, moves it
into the mailbox with `list::splice`, and consumes the operation and reply
token only after admission succeeds. Domain state that must change with a
completion, such as Actor membership or a STREAM binding, is committed under
the same node lock. Payload preparation, ready-index admission, or wire-send
failure therefore leaves the operation and token retryable and leaves domain
state unchanged.

Closing a bound STREAM session can invoke the raw socket's session observer,
so it does not disconnect while holding the node lock. This path first
prepares the operation, terminal record, and ready-index node and excludes
competing completion. It then releases the node lock, disconnects, and commits
the prepared terminal record without allocation. Preparation failure returns
before disconnect, while disconnect failure cancels the reservation; the node
and raw STREAM lock orders therefore cannot form a cycle.

Timeout completions are produced by the global timeout scheduler (an immortal
singleton with its own thread) calling `complete_pending_operation()` at the
deadline. The timeout callback waits for the request transaction to commit or
cancel, so it cannot complete an operation before request delivery. When a
shutdown outlives its deadline it completes every remaining operation
(including timeout-less ones) with exactly one
`REQUEST_TERMINATED`/`ESHUTDOWN` completion. Shutdown re-entry is `EDEADLK`
only through the `shutdown_active` flag; a sequential call after TIMED_OUT is
legal.

A committed operation owns its timeout task in
`pending_operation_t::timeout_task`. `operation_timeout_guard_t::commit()`
hands the task to that field before opening the scheduler gate, so a committed
timer can never outlive its operation and fire against a recycled node address
or serial; the timeout callback checks both `operation_id.high` (the node
lifecycle generation) and `low` to bar the ABA. Every terminal path (normal,
owner-missing, and the two-phase STREAM commit) and node destroy/shutdown erase
the operation through the single `detach_pending_operation_locked()` primitive,
which owns the task handoff and erase and returns the task for the caller to
cancel *outside* the node lock (a firing timeout handler takes that lock, and
the scheduler recognises a self-cancel) — so no new terminal path can drop the
handoff. `lifecycle_generation` is issued at node creation by
`allocate_lifecycle_generation()`, anchored on epoch wall-clock microseconds
(not boot-relative `now_ms()`) and strictly monotonic in-process via a CAS.
Core owns no durable state, so a same-microsecond restart or a rolled-back
clock surfaces as the §5 duplicate/stale generation admission conflict, never
as silent replacement.

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
  `PEER_REJECTED` event on both sides. A higher lifecycle generation starts a
  separate lifetime in a fresh entry and leaves the previous admitted entry
  `DRAINING` (excluded from new snapshots, closed with the transport or an
  explicit disconnect, `PEER_DRAINING` emitted). The descriptor advertises
  the node's bind endpoint, and an inbound-observed peer records it under
  source `DISCOVERY` — a manual intent for the same endpoint merges into one
  `MIXED` entry, and removing one source keeps the connection under the
  other.
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
3. Once the reserve passes, the local slots (mailbox list nodes and
   ready-index keys) are pre-reserved before the remote commit, pulling the
   last fallible step ahead of any commit. A failure rolls everything back
   and returns `ENOMEM`; the reservations only exist while the node mutex is
   held (every failure path rolls back before unlocking).
4. Commit is the local fanout (shared `zlink_msg_copy` refcounts) plus exactly
   one wire submit per peer. An admitted peer that loses its pipe between the
   reserve and the commit is classified as a peer departure, not a drop, and
   is counted as unreachable (spec §7). A receiving node fans out only to its
   own local subscription matches and has no re-propagation path (structural
   no-relay).

The publish detail and the `MULTICAST_COMMITTED`/`MULTICAST_DROPPED` events
report the snapshot/admitted/dropped/unreachable counts verbatim
(remote snapshot = admitted + dropped + unreachable).

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

One monitor per node (bounded queue). Emitters pin the monitor pointer under
the node mutex (`monitor_emit_refs`), and close deletes the object only after
that count drains to zero. `emit_monitor_event` applies the mask
and enqueues; on overflow it aggregates high-frequency kinds
(MESSAGE_SUBMITTED, BACKPRESSURED) while preferring to keep peer state,
protocol error and lifecycle events. Counters accumulate independently of
event consumption and the status query is an atomic snapshot. Handler and recv
are the single consumer of the same queue; re-entrant deregistration inside
the handler is `EDEADLK`. `CLAIM_REVOKED` is emitted by the shutdown revoke
loop after it drops the node lock.

The raw socket monitor (`api/monitoring/`) is a separate layer from the
MeshNode monitor, owned by one global handler registry. Every reader that
dereferences registry state (status, receive-model, handler registration and
close checks) holds a `monitor_state_pin_t`, and close/unregister wait for the
pins to drain before deleting the storage (no UAF). A handler update passes its
pinned state as the expected value, checks entry identity and `unregistered`
under the registry lock, and fails with `ESHUTDOWN` rather than resurrecting a
registration on a closing socket. Registration completes its identity (handler,
provider, subject, dispatch task id) under `dispatch_sync`, so an immediate
dispatch tick's first callback — and that callback's self-close finalizer —
observes only the fully committed task id. The service-control scheduler that
runs the dispatch task allocates the task's schedule node once at add time and
keeps wakeup and every periodic tick allocation-free (and non-throwing) by
reusing that node handle; task callbacks and the worker seal `bad_alloc` so a
failed tick is dropped while the active-task epilogue always runs.

## 9. Locking rules and thread inventory

| Thread | Owner | Role |
|---|---|---|
| Application threads | caller | all public APIs, direct wire sends |
| Ingress thread | 1 per node | ROUTER recv, socket monitor drain, remote record admission, completion delivery |
| Timeout scheduler | 1 per process (immortal) | timeout completions at operation deadlines |
| Spot timer scheduler | 1 per MeshNode (lazily created, reclaimed at destroy) | Spot timer fires and turn waits, isolated from the global timer scheduler so a claim wait never blocks other nodes or plain timers |

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
