[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

<!-- zlink-nav:start -->
[← SPOT](07-3-spot.md) | [Routing ID →](08-routing-id.md)
<!-- zlink-nav:end -->

# SPOT Actor Guide

This guide covers Actor creation, Spot join/leave, teardown, and session binding.
For SPOT basic setup and dispatch handler registration see the [SPOT guide](07-3-spot.md).
For exact API contracts see the [SPOT spec](../api/spot.md).

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
3. Attach the STREAM socket to the session owner `SpotNode` with
4. Bind session and Actor with `zlink_stream_bind_actor()`.
5. Inside the STREAM packet handler or app logic, select an Actor id and call
   `zlink_stream_send_bound_actor_part()`.
6. When `ACTOR_READABLE` arrives in the dispatch callback, copy the `subject`
   Actor ref and drain it with `zlink_spot_node_actor_recv_part()`.

```c
zlink_actor_ref_t ref;
zlink_spot_node_actor_new(node, "player-42", &ref);


/* async submit; bind completion fires through the reply handler */
zlink_stream_bind_actor(stream, &session_rid, &ref,
                        my_bind_handler, userdata, 2000);

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_stream_send_bound_actor_part(
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

Actor location belongs to the SPOT/Actor lifecycle. Creating an Actor places
it on the Entry Spot, a successful join moves it to the joined user Spot, and
a successful explicit leave moves it back to the Entry Spot. STREAM session
bind and unbind do not change the Actor location.

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

To obtain a checked ref for an Actor that lives on a remote node, use the
async `zlink_remote_actor_get_ref()` lookup. The call does not block; the
completion handler delivers the lookup outcome. On success
`result->actor` holds the checked ref; otherwise the completion fails with a
not-found-, not-connected-, or timeout-class result.

```c
static void on_lookup(
    const zlink_actor_lookup_result_t *result, void *userdata)
{
    if (result->result == ZLINK_REQUEST_OK) {
        zlink_actor_ref_t ref = result->actor;  /* copy inside callback */
        /* use ref for join, bind, destroy, etc. */
    }
}

zlink_remote_actor_get_ref(
    node,                /* SpotNode submitting the lookup */
    &target_node_rid,
    "player-42",
    on_lookup,
    NULL,                /* userdata */
    3000);               /* timeout_ms */
```

The remote Actor create API and admission handler have been removed. To place
an Actor that must originate on a remote node, the application creates it on
that SpotNode directly via `zlink_spot_node_actor_new()` — either using the
remote node's local handle inside the same process, or by sending an
application-level RPC that asks the remote node to perform the creation.
Afterwards, `zlink_spot_node_actor_join_spot()` can move the Actor to a
specific user Spot, and the final ref returned by the join completion is used
for any subsequent work.

## 2. Spot join

To move an Actor into a user Spot, send a join request with
`zlink_spot_node_actor_join_spot()`. The target Spot receives an
`ACTOR_JOIN_READABLE` dispatch event, reads the request payload with
`zlink_spot_actor_join_recv()`, and sends an accept or reject reply with
`zlink_spot_actor_join_reply()`. Join completion is delivered through the
dedicated `zlink_actor_join_spot_handler_fn` and carries the final Actor ref and
joined Spot rid.

```c
static void on_join(
    const zlink_actor_join_result_t *result,
    zlink_msg_t *parts, size_t part_count, void *userdata)
{
    if (result->result == ZLINK_REQUEST_OK) {
        /* success: result->actor is the final Actor ref
           (target node ref for remote join) */
        zlink_actor_ref_t final_ref = result->actor;
        /* use final_ref for follow-up Actor calls or location moves */
    }
    zlink_multipart_close(parts, part_count);
}

zlink_spot_node_actor_join_spot(
  node, &actor_ref,
  &dest_node_rid, &dest_spot_rid,
  &payload_msg, 1,    /* join state payload parts */
  on_join,            /* zlink_actor_join_spot_handler_fn completion */
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
        int join_result = 0; /* 0 = accept; non-zero = reject code */
        zlink_spot_actor_join_reply(spot_, &info, join_result, NULL, 0);
        zlink_multipart_close(parts, part_count);
    }
    break;
}
```

A new join, leave, or destroy while a join is pending fails with a busy-class
result. **An Actor can join a user Spot without a bound STREAM session.** Actor
location transitions and session attach are independent state transitions.
`dest_spot_rid` must be a user Spot on the target node; Entry Spot is not a
valid target. Idempotent join on the same Spot completes successfully without
going through admission. `timeout_ms` is the operation timeout; whether the
submit stage returns immediately on failure is controlled by `ZLINK_DONTWAIT`
in flags.

> If the target user Spot has no dispatch handler installed, the join is not
> auto-accepted. With `timeout_ms > 0` it stays pending until timeout; with
> `timeout_ms == 0` it stays pending until a handler is installed or the
> Spot/SpotNode terminates.

### Spot lifecycle events

To observe Actor location changes (creation, join, leave, destroy) on a Spot, install `zlink_spot_dispatch_event_handler()` before the Actor transition can occur. When it reports `ACTOR_LIFECYCLE_READABLE`, drain events with `zlink_spot_recv_actor_lifecycle()`.

```c
zlink_spot_actor_lifecycle_event_t event;
while (zlink_spot_recv_actor_lifecycle(spot, &event, ZLINK_DONTWAIT) == ZLINK_RECV_OK) {
    if (event.kind == ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED) {
        /* event.info.current_actor entered this Spot */
    }
}
```

Lifecycle events are observation-only. For deciding join completion, the application uses the join completion handler and the final Actor ref it returns.

## 3. Spot leave

`leave` is an async submit API that moves an Actor from its current Spot back
to the same node's Entry Spot. If the Actor is already in the Entry Spot, the
call is idempotent success and no lifecycle events fire. After a successful
leave, Actor messages surface through the Entry Spot dispatch event.

```c
static void on_leave(
    zlink_request_result_t result,
    zlink_msg_t *parts, size_t part_count, void *userdata)
{
    /* No completion payload; the result code decides success/failure. */
}

zlink_spot_node_actor_leave_spot(
  node, &actor_ref,
  &current_spot_rid,  /* Actor's current Spot rid */
  on_leave,
  userdata,
  2000);
```

`current_spot_rid` is an optimistic check to prevent stale leaves. If the
caller's view of the current Spot differs from the actual current Spot, the call
fails with an invalid-state-class result. Leave fails as busy-class while a join
is pending; leave does not cancel a pending join. A leave that actually moves
the Actor from a user Spot to the Entry Spot updates the active route to the
Entry Spot location.

## 4. Actor teardown

Actor destroy succeeds only while the Actor is in the Entry Spot. If the
Actor is in a user Spot, call `leave` first to return it to Entry. Destroy is
also an async submit API.

```c
static void on_destroy(
    zlink_request_result_t result,
    zlink_msg_t *parts, size_t part_count, void *userdata) { /* ... */ }

/* leave first, then submit destroy inside the leave completion */
zlink_spot_node_actor_destroy(node, &ref, on_destroy, userdata, 2000);
```

On successful destroy, the Entry Spot Actor slot is removed; if the active
route currently points at the same Actor ref, the route is also removed.
Session attach state is independent of Actor location, so explicit
`zlink_stream_unbind_actor()` is required if the session attach must be torn
down separately. Use `zlink_spot_node_actor_close_bound_session()` to also
close the client connection. When a STREAM session closes or disconnects, its
Actor bindings are cleared automatically, but this does **not** move the Actor
to the Entry Spot or change its joined Spot. To enumerate a session's bound
Actors, call `zlink_stream_bound_actors()`.

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

---
<!-- zlink-nav:bottom:start -->
[← SPOT](07-3-spot.md) | [Routing ID →](08-routing-id.md)
<!-- zlink-nav:bottom:end -->
