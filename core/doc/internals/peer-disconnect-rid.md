[English](peer-disconnect-rid.md) | [한국어](peer-disconnect-rid.ko.md)

# Peer Disconnect by Routing ID Internals

This document describes the ownership boundaries used by
`zlink_disconnect_rid()` and `zlink_spot_node_disconnect_peer_rid()`.

## Socket Path

```text
+---------------------+
| public C API        |
+---------------------+
| socket_base_t       |
+---------------------+
| socket-specific map |
+---------------------+
| pipe termination    |
+---------------------+
```

The public C API validates the handle and delegates the operation to
`socket_base_t::term_peer_rid()`. That function first checks whether the
socket is attached to Discovery. Attached sockets are owned by Discovery for
lifecycle changes, so manual disconnect is rejected with `EFSM` while attached
(or `ESHUTDOWN` during shutdown).

ROUTER and STREAM have routing maps and use map lookup to find the target
pipe. STREAM uses a local connection id as its 4-byte routing id, so the input
rid length must be `sizeof(uint32_t)`.

Other sockets scan the current attached-pipe snapshot for a pipe whose own
routing id, or whose peer pipe's routing id, matches the target rid. If more
than one pipe matches, the destructive target is ambiguous; the call returns
`EADDRINUSE` and terminates no pipe.

## Duplicate RID Policy

`options_t::rid_duplicate_policy` stores the common socket option
`ZLINK_OPT_RID_DUPLICATE_POLICY`. The default is
`ZLINK_RID_DUPLICATE_REJECT`.

ROUTER reads the same stored duplicate-policy state when it decides whether a
new pipe may replace an existing peer identity.

## SpotNode Path

SpotNode keeps an index from discovery provider node routing id to endpoint
set. `zlink_spot_node_disconnect_peer_rid()` finds the endpoint set for the
target node rid and sends the same control command used by endpoint-based
disconnect for each endpoint.

The Spot facade has no separate function. Peer connections and mesh sockets
are owned by the SpotNode runtime, not by individual Spot facades.
