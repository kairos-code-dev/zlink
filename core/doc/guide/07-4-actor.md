[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

<!-- zlink-nav:start -->
[← SPOT](07-3-spot.md) | [Routing ID →](08-routing-id.md)
<!-- zlink-nav:end -->

# SPOT Actor Guide

This document explains Actor creation, Spot join/leave, messaging,
termination, STREAM session binding and the transfer flow. For MeshNode setup
and the claim consumption flow see the [SPOT guide](07-3-spot.md). The exact
function contracts are owned by the
[Actor spec](../../../framework/doc/framework/spec/server/23-spot-actor.ko.md) and the
[STREAM session spec](../../../framework/doc/framework/spec/server/31-session-actor-dispatch.ko.md).

> For **what an Actor is and when** to use one (session-to-processing
> binding, reconnect continuity, difference from a plain Spot), start with
> [Services overview §mental model](07-0-services.md#12-mental-model--which-layer-to-use-when).

## 1. Creating Actors and the entry Spot

An Actor is an addressable unit identified by `zlink_actor_ref_t` (actor id +
generation). It owns no socket and no in-process endpoint, and is always
joined to some Spot. Right after creation it belongs to the node's entry
Spot.

```c
zlink_actor_ref_t player;
zlink_request_result_t rc = zlink_mesh_node_actor_new(
  node, "player-9421", NULL, 0, &player, 0, 2000);
/* Re-creating the same id is ZLINK_REQUEST_CONFLICT (EEXIST). */
```

- Creation leaves a CREATED record on the entry Spot's `SPOT_CONTROL` lane.
  The entry Spot's claim consumer performs initial authentication and routing
  from that record.
- Location lookup is `zlink_mesh_node_actor_lookup()` locally (`ENOENT`) and
  `zlink_mesh_node_actor_lookup_remote()` against a specific remote node (the
  location returns in the completion). There is no mesh-wide search API —
  distributed location is the framework location layer's responsibility.

## 2. Actor messaging

```c
/* node → actor */
zlink_mesh_node_send_to_actor(node, &player, NULL, &part, 1, 0);
zlink_mesh_node_request_to_actor(node, &player, NULL, &part, 1, &op, 0, 3000);

/* actor → actor (calls that name their source) */
zlink_actor_request_to_actor(node, &player, &dealer, NULL, &part, 1, &op, 0, 3000);
```

- Messages enqueue directly into the Actor mailbox and surface as
  `ACTOR_SEND`/`ACTOR_REQUEST` records on that Actor owner's application
  claim. FIFO is preserved per owner.
- A stale-generation ref fails with `ESTALE`; a missing Actor completes with
  `ENOENT`.
- Request completions return on the caller's (Node or source Actor) owner
  infrastructure lane.

## 3. Spot join / leave

```c
/* Ask to join a room on another node (travels the wire when remote). */
zlink_mesh_node_actor_join_spot(node, &player, &room_node_rid, &room_spot_rid,
                                room_spot_generation, NULL, 0, &op, 3000);
```

Join is an admission procedure:

1. The request arrives as a JOIN record on the target Spot's `SPOT_CONTROL`
   lane.
2. The target consumer decides with
   `zlink_actor_join_reply(&record->reply_token, ZLINK_ACTOR_JOIN_ACCEPTED,
   ...)`. That token is join-only: passing it to `zlink_mesh_reply()` is
   `EINVAL`.
3. Only ACCEPTED commits membership (epoch+1). The requester receives the
   result and the new epoch in the completion.

`zlink_mesh_node_actor_leave_spot()` performs the entry-Spot return with an
expected-epoch CAS, and `zlink_mesh_node_actor_join_entry_spot()` sends the
Actor to a chosen node's entry Spot. `zlink_mesh_node_actor_destroy()` drains
the mailbox and ends with a terminal completion.

## 4. STREAM session binding

External byte sessions (game clients and similar) arrive over a raw STREAM
socket. The session-to-Actor association is owned by the STREAM session
service.

```c
void *svc = zlink_stream_session_service_new(node, stream_socket);
zlink_stream_session_service_start(svc);

/* Session routing id ↔ Actor binding (generation CAS, idempotent). */
zlink_mesh_operation_id_t bind_op;
zlink_stream_session_bind_actor(svc, &session_rid, &player, &bind_op, 2000);

/* Relay session bytes to the Actor. */
zlink_stream_session_send_to_actor(svc, &session_rid, &player, NULL, &part, 1, 0);

/* Reply only to the binding generation carried by the Actor record. */
zlink_mesh_node_actor_send_bound_session(
  node, &player, actor_record->source_binding_generation, &part, 1, 0);
```

- One session may bind several Actors; the binding CAS validates generations.
- The Actor handler reads the session routing ID and binding generation from
  `source_spot_rid` and `source_binding_generation` in the received record. A
  record from an earlier binding is rejected with `ESTALE` after unbind and
  rebind.
- A session disconnect removes only that session's bindings and never changes
  an Actor's joined Spot — bind the reconnected session to the same Actor and
  the entity continues (reconnect continuity).
- `zlink_mesh_node_actor_close_bound_session()` closes the session bound to
  an Actor.

## 5. Actor transfer (moving)

The data plane for moving an Actor to another node is owned by Core; the
decision to move and the distributed locking (lease, participant CAS) are
owned by the framework transfer authority.

```
source node                                target node
  prepare  ──token──▶ (framework decides and coordinates)
  │ fence: new app messages EAGAIN           prepare (placeholder, staged)
  │ frozen mailbox resend ──TRANSFER_DATA──▶ staged accumulation (ACK)
  commit(token, epoch+1)                     activate: staged→mailbox, unfence
  actors entry removed (residual infra stays drainable)
```

- `zlink_mesh_node_actor_transfer_prepare()` issues the 64-byte sealed token
  and fences that Actor's application lane (submits `EAGAIN`, claims `EBUSY`).
- `..._transfer_commit()` validates the token, transfer ID, generation and
  exactly-next epoch. A stale token is `ESTALE`; a duplicate commit is
  `EALREADY`.
- `..._transfer_activate()` moves staged records into the mailbox on the
  target and lifts the fence. `..._transfer_abort()` restores state from any
  phase.
- Replies to requests caught mid-transfer are relayed by Core back to the
  original requester.

## 6. Common mistakes

- **Stand up the entry Spot consumer first.** If nobody claims CREATED/JOIN
  records, creation and join flows end in completion timeouts.
- **Never mix join tokens with generic tokens.** A JOIN record's token is for
  `zlink_actor_join_reply()` only.
- **Store refs with their generation.** Re-creating an Actor changes the
  generation, and calls with a stale ref end with `ESTALE`.
- **Use transfer through the framework.** The Core transfer API is the
  low-level fence protocol coordinated by the framework authority;
  applications rarely call it directly.

---
<!-- zlink-nav:bottom:start -->
[← SPOT](07-3-spot.md) | [Routing ID →](08-routing-id.md)
<!-- zlink-nav:bottom:end -->
