[한국어](README.ko.md) | English

[Specification index](../../README.md) · [Core index](../README.md)

# Service API

In the Core 10.1.0 service layer, MeshNode owns transport location, service
mailboxes, and request correlation, while Spot, Actor, and STREAM session
provide their logical state and receive owners. Raw sockets have no knowledge
of service objects. Services do not expose raw frames, raw pipes, or internal
peer state. The public peer snapshot defined by the MeshNode contract remains
available and contains only documented peer identity, membership, lifecycle,
and admission state.

| Document | Responsibility |
|---|---|
| [MeshNode](01-mesh-node.md) | MeshName, ChannelName membership, peer admission, node/channel messaging, and Logical Multicast submit |
| [Dispatch](02-dispatch.md) | Application and infrastructure readiness, per-domain claims, receive batches, operations, and replies |
| [Spot](03-spot.md) | Spot lifecycle, direct messaging, local subscriptions, and Logical Multicast receive |
| [Actor](04-actor.md) | ActorRef, Actor mailboxes, Spot membership, lifecycle, and transfer fences |
| [STREAM session](05-stream-session.md) | Raw STREAM and MeshNode association, session-to-Actor bindings, bidirectional delivery, and barriers |

Classic PUB/SUB and generic STREAM are independent contracts in the [socket index](../socket/README.md).

## Common contract for versioned service structures

Every service structure whose first two fields are `struct_size` and `version`
belongs to one of the following ownership classes. This table owns the common
size, version, and error contract used by the individual owner specifications.

| Class | Structures and use sites |
|---|---|
| Caller-initialized input | `zlink_mesh_node_options_t`, `zlink_mesh_peer_connection_options_t`, `zlink_actor_transfer_prepare_t`, `zlink_mesh_monitor_open_options_t` |
| Caller-initialized output | `zlink_mesh_publish_detail_t`, `zlink_mesh_node_status_t`, `zlink_mesh_peer_entry_t`, `zlink_spot_status_t`, `zlink_actor_location_t` for local lookup, `zlink_actor_transfer_prepare_result_t`, `zlink_mesh_receive_requirements_t`, `zlink_stream_session_binding_t`, `zlink_stream_session_status_t`, `zlink_mesh_monitor_event_t` for monitor receive, `zlink_mesh_monitor_status_t` |
| Core-owned read-only view | `zlink_mesh_ready_record_t` in a ready batch, `zlink_mesh_receive_record_t` in a receive batch, `zlink_mesh_send_ready_data_t`, `zlink_actor_control_record_t`, `zlink_actor_join_completion_t`, and `zlink_actor_transfer_control_t` in `kind_data`, `zlink_actor_location_t` in completion `kind_data`, and `zlink_mesh_monitor_event_t` in a monitor callback |

Before a call, the caller sets every caller-initialized input and output to
`struct_size = sizeof(the type)` and `version = 1`. For an output array, the
caller initializes every element within its capacity in the same way. Core
checks that `struct_size` is at least the size of the current public type and
that `version == 1` before inspecting any other field. It neither reads nor
writes a tail beyond the known current type size. A short structure or a
different version fails with `errno == EINVAL` and does not partially write the
output payload.

| API result family | Result for a size/version failure |
|---|---|
| Handle-returning constructor or monitor open | `NULL` with `errno == EINVAL` |
| Connect | `ZLINK_CONNECT_INVALID_ARGUMENT` with `errno == EINVAL` |
| Submit | `ZLINK_SUBMIT_INVALID_ARGUMENT` with `errno == EINVAL` |
| Synchronous request | `ZLINK_REQUEST_INVALID_ARGUMENT` with `errno == EINVAL` |
| Configuration or query | `ZLINK_CONFIG_INVALID_ARGUMENT` with `errno == EINVAL` |
| Receive | `ZLINK_RECV_INVALID_STATE` with `errno == EINVAL` |

The caller retains ownership of caller-initialized output storage. On success,
Core fills the complete public prefix for the current version. Core initializes
a Core-owned view with `struct_size = sizeof(the type)` and `version = 1`; the
caller does not initialize, modify, or free it. Such a view and its internal
pointers remain valid only for the callback call or owner-batch lifetime. When
one type has two use sites, the class is selected by use site. For example, a
local Actor lookup uses caller-initialized storage, while an Actor location in a
completion is a receive-batch-owned view.
