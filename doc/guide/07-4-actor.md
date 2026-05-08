[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

# SPOT Actor Guide

This guide covers Actor creation, Spot join/leave, teardown, and session binding.
For SPOT basic setup and dispatch handler registration see the [SPOT guide](07-3-spot.md).
For exact API contracts see the [SPOT spec](../spec/core/service/spot.md).

## 1. Distributing session messages with Actors

Actors let you route messages from a STREAM client session to a specific
processing unit and distinguish the drain target in the Spot dispatch callback.
One session can be bound to multiple Actors; one Actor is bound to at most one
session at a time.

An Actor belongs to the `Entry Spot` immediately after creation. The `Entry Spot`
is the default Spot that every `SpotNode` always maintains. Registering a dispatch
handler on the `Entry Spot` lets the application receive initial Actor messages,
perform authentication, or select a target Spot.

Obtain an Entry Spot facade as follows:

```c
void *entry = NULL;
zlink_spot_node_entry_spot(node, &entry);
zlink_spot_dispatch_event_handler(entry, my_dispatch_handler, userdata);
```

Close the facade with `zlink_spot_destroy(&entry)` when done. The Entry Spot itself
is owned by the `SpotNode` and is not destroyed when the facade is closed.

The minimal flow is:

1. Create an Actor on the `SpotNode`.
2. Identify the STREAM client session routing id.
3. Bind session and Actor with `zlink_stream_bind_actor()`.
4. Inside the STREAM packet handler or app logic, select an Actor id and call
   `zlink_stream_send_bound_actor_part()`.
5. When `ACTOR_READABLE` arrives in the dispatch callback, copy the `subject`
   Actor ref and drain it with `zlink_spot_node_actor_recv_part()`.

```c
zlink_actor_ref_t ref;
zlink_spot_node_actor_new(node, "player-42", &ref);

zlink_stream_bind_actor(node, stream, &session_rid, &ref, 2000);

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_stream_send_bound_actor_part(
  node,
  stream,
  &session_rid,
  "player-42",
  &part,
  0,
  ZLINK_PART_FINAL);
```

When the Actor has readable parts the dispatch callback identifies the drain
target:

```c
case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE: {
    const zlink_actor_ref_t *subject_ref =
      (const zlink_actor_ref_t *) info_->subject;
    zlink_actor_ref_t actor = *subject_ref;
    for (;;) {
        zlink_actor_recv_info_t recv_info;
        zlink_msg_t part;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = zlink_spot_node_actor_recv_part(
          node,
          &actor,
          &recv_info,
          &part,
          &more,
          ZLINK_DONTWAIT);

        if (rc == ZLINK_RECV_NO_DATA)
            break;
        if (rc != ZLINK_RECV_OK)
            break;

        /* process part */
        zlink_msg_close(&part);
    }
    break;
}
```

To make an Actor address discoverable from another node, enable
`ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC` on the Actor owner Discovery, bind the
Actor to a STREAM session, then query with `zlink_discovery_resolve_actor()`.
Creating an Actor or joining a Spot alone does not publish an active route.
**Active route is published only at STREAM session bind success.**

To look up an existing Actor by id on the local node:

```c
zlink_actor_ref_t ref;
zlink_config_result_t rc = zlink_spot_node_actor_lookup(node, "player-42", &ref);
if (rc == ZLINK_CONFIG_OK) {
    /* actor exists — use ref */
} else if (rc == ZLINK_CONFIG_NOT_FOUND) {
    /* no actor with that id */
}
```

To create an Actor on a remote node, use
`zlink_spot_node_create_remote_actor()`. When the same actor id already exists
on the target node, the call returns the existing result without creating a
new slot. When the target node rejects the request in its admission handler,
the request ends with a rejected result. Remote create-or-get does not go through
the target Spot join handler. A newly created remote Actor belongs to the target
node's Entry Spot.

To control whether remote actor creation is permitted, register an admission
handler on the `SpotNode`. The handler runs synchronously for every
`zlink_spot_node_create_remote_actor()` request addressed to this node:

```c
zlink_actor_admission_result_t my_admission(
  void *node_,
  const char *actor_id_,
  const zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    /* inspect actor_id and optional payload parts */
    if (/* reject condition */)
        return ZLINK_ACTOR_ADMISSION_REJECT;
    return ZLINK_ACTOR_ADMISSION_ACCEPT;
}

zlink_spot_node_actor_admission_handler(node, my_admission, userdata);
```

Pass `NULL` as the handler to remove a previously registered handler (all
remote create requests are accepted by default).

## 2. Spot join

To move an Actor into a user Spot, send a join request with
`zlink_spot_node_actor_join_spot()`. The target Spot receives an
`ACTOR_JOIN_READABLE` dispatch event, reads the request payload with
`zlink_spot_actor_join_recv()`, and sends an accept or reject reply with
`zlink_spot_actor_join_reply()`.

```c
/* send join request */
zlink_spot_node_actor_join_spot(
  node, &actor_ref,
  &dest_node_rid, &dest_spot_rid,
  &payload_msg, 1,    /* join state payload parts */
  my_join_handler,    /* completion callback */
  userdata,
  0, 3000);           /* flags, timeout_ms */

/* inside target Spot dispatch callback */
case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE: {
    zlink_actor_join_info_t info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    while (zlink_spot_actor_join_recv(spot_, &info, &parts, &part_count,
                                      ZLINK_DONTWAIT) == ZLINK_RECV_OK) {
        /* info.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE means remote join */
        int accept = /* inspect payload */;
        zlink_spot_actor_join_reply(spot_, &info, accept, NULL, 0);
        zlink_multipart_close(parts, part_count);
    }
    break;
}
```

A new join, leave, or destroy while a join is pending fails with a busy-class
result. Joining a user Spot outside Entry requires the source Actor to have a
bound STREAM session. `timeout_ms` is the operation timeout; whether the submit
stage returns immediately on failure is controlled by `ZLINK_DONTWAIT` in flags.

## 3. Spot leave

`leave` moves an Actor from its current Spot back to the Entry Spot. If the
Actor is already in the Entry Spot, the result is idempotent success. After a
successful leave, Actor messages surface through the Entry Spot dispatch event.

```c
/* return from game Spot to Entry Spot */
zlink_spot_node_actor_leave_spot(
  node, &actor_ref,
  &current_spot_rid,  /* Actor's current Spot rid */
  2000);
```

`current_spot_rid` is an optimistic check to prevent stale leaves. If the
caller's view of the current Spot differs from the actual current Spot, the call
fails with an invalid-state-class result. Leave fails with `EBUSY` while a join
is pending; leave does not cancel a pending join.

## 4. Actor teardown

Actor destroy is permitted only while the Actor is in the Entry Spot. If the
Actor is in a user Spot, call `leave` first to return it to Entry.

```c
/* leave first */
zlink_spot_node_actor_leave_spot(node, &ref, &current_spot_rid, 2000);
/* confirm return to Entry Spot, then destroy */
zlink_spot_node_actor_destroy(node, &ref, 2000);
```

If a bound STREAM session exists when destroy is called, the session Actor list
entry and the Actor's bound session ref are cleaned up first. The client
connection itself is not closed. To close the connection as well, call
`zlink_spot_node_actor_close_bound_session()` before destroy.

To push a message to the STREAM client from the Actor side (without a recv
triggering it), use `zlink_spot_node_actor_send_bound_session_msg()`. This
sends directly on the bound STREAM session. The Actor must have an active bound
session; otherwise the call fails.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "push!", 5);
zlink_spot_node_actor_send_bound_session_msg(node, &ref, &msg, 0);
```

## 5. Actor C samples

C samples showing three Actor patterns:

| Pattern | File |
|---------|------|
| Per-room Actor dispatch | `bindings/c/samples/actor_room_server_sample.c` |
| Gateway session relay to remote Actor | `bindings/c/samples/actor_gateway_relay_sample.c` |
| Single-user queue serialization | `bindings/c/samples/actor_single_player_queue_sample.c` |
