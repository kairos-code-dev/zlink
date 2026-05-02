[English](registry.md) | [한국어](registry.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md)

# Registry

## API Surface

- Registry is the global service directory and topology summary source.
- Use `zlink_registry_topology_snapshot()` for local in-process summary access.
- Use `zlink_registry_query_client_*()` and `zlink_registry_query_snapshot()`
  for remote summary queries.
- Registry topology is intended for global summary only. For detailed local
  state transitions, compare successive snapshot/query results.

The Registry is the central service directory for the zlink service layer. It
accepts service registration, deregistration, and heartbeat requests from
SPOT Nodes and socket family services, and periodically broadcasts the
aggregated service list to all connected Discovery instances.

## Final Rule For SPOT Address Ownership

In a managed SPOT configuration, Registry is the final rule for deciding which
`SpotNode` currently owns a `spot_rid` inside the same current
`service_name` view. This document defines how the
`(service_name, spot_rid) -> owner_node_rid` mapping is registered, replaced,
and removed.

- `spot_rid` is a logical spot-address key interpreted inside the current
  `service_name`.
- `owner_node_rid` identifies the `SpotNode` that currently owns that name.
- The `dest_node_rid + dest_spot_rid` pair used by SPOT direct submit is built
  from this information.
- Discovery may keep nearby cached copies, but it is not the final authority.

The SPOT routed public API itself is defined in [spot.md](spot.md). Discovery
cache and resolve flow are defined in [discovery.md](discovery.md).

Applications normally use `zlink_discovery_resolve_spot()` for lookup. Registry
manages the backing data that makes that answer correct.

## spot ownership lifecycle

This section defines how Registry registers, refreshes, replaces, and withdraws
logical `spot_rid` ownership.

### 1. Registration point

A `Spot` becomes eligible for ownership registration once its `spot_rid` is
fixed, its backing `SpotNode` `node_rid` is known, and it is participating in
Registry.

- The registration key is `spot_rid`.
- The registration value is the current owner `node_rid`.
- A repeated advertisement from the same `Spot` with the same `spot_rid` may be
  treated as a refresh.
- Until ownership advertisement completes, the spot must not be treated as an
  active destination for logical-address-based direct submit.

### 2. Minimum ownership record meaning

The Registry ownership record must be able to represent at least the following
meaning.

- `service_name`: logical service scope for this address
- `spot_rid`: logical spot-address key
- `owner_node_rid`: current authoritative owner node
- `ordering token`: value used to compare newer vs. older ownership claims
- `state`: ownership state such as active, replaced, or withdrawn
- `last_reported_ms`: most recent registration or refresh time

The public API does not need to expose these exact field names, but observable
behavior must preserve those meanings.

### 3. Duplicate claim and handover

Multiple `Spot` instances may attempt to register the same `spot_rid`. Registry
decides the authoritative owner according to the handover setting.

- handover off: the earlier active owner remains authoritative. A later
  duplicate claim may be rejected or stored as inactive, but the authoritative
  owner must not change.
- handover on: the owner with the newer ordering token becomes authoritative.
  The previous owner must move to a replaced state.
- An older update must never roll the current authoritative owner back.

This rule follows the same direction as
`ZLINK_OPT_RID_DUPLICATE_POLICY = ZLINK_RID_DUPLICATE_HANDOVER`. The decision
must be based on the ordering token, not merely on message arrival order. The
concrete source of the handover setting is implementation-defined in this
document version. A conforming implementation may derive it from the routed
duplicate-policy setting already applied to the owning service or node.

The current core implementation keeps topology rows separately per endpoint for
the same `(service_name, spot_rid)` and then picks exactly one authoritative
owner during owner lookup. The concrete ordering token used by that
implementation is:

- Primary: registration time of the still-live `SpotNode` provider in the same
  `service_name`
- Secondary: topology row `last_reported_ms` when provider registration time is
  equal
- Tertiary: endpoint string comparison when both previous values are equal

This rule prevents an older owner from taking ownership back just because a
late topology report arrives after a newer owner is already live.

Registry must also exclude endpoints that are no longer present in the current
live provider set when choosing the authoritative owner. A stale topology row
must not remain authoritative after its provider has disappeared.

### 4. Unregister and tombstone

When the authoritative owner exits or the corresponding `Spot` is destroyed,
the ownership must be withdrawn.

- On normal shutdown, unregister should be attempted immediately.
- Because abnormal exit or network partition may prevent unregister delivery,
  Registry must be able to retire stale ownership through a lease, heartbeat,
  or equivalent expiration rule.
- To prevent stale cache rollback immediately after handover or withdraw,
  Registry may keep a short tombstone or withdrawn marker.

The concrete tombstone TTL is implementation policy. However, the ordering
guarantee must be strong enough that an older advertisement cannot overwrite a
newer owner or a withdrawn state.

### 5. Scale-out assumptions

This ownership model must support high-cardinality deployments. The
implementation should allow scenarios on the order of 10,000 nodes with 10,000
spots per node, or similarly large logical `spot_rid` populations.

- The Registry ownership store may live in a single instance or be distributed
  across multiple shards.
- The public contract requires authoritative lookup semantics, not a single
  process storage layout.
- Expiration must not require a per-spot heartbeat that scales linearly with
  total spot count.

For aggregated liveness maintenance and cache-side scale-out rules, see
[discovery.md](discovery.md).

## Constants

### Registry State

```c
typedef enum zlink_registry_state_t
{
    ZLINK_REGISTRY_STATE_IDLE     = 1,
    ZLINK_REGISTRY_STATE_ACTIVE   = 2,
    ZLINK_REGISTRY_STATE_DEGRADED = 3,
    ZLINK_REGISTRY_STATE_ERROR    = 4
} zlink_registry_state_t;
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_REGISTRY_STATE_IDLE` | 1 | Registry created but not yet started |
| `ZLINK_REGISTRY_STATE_ACTIVE` | 2 | Registry running normally |
| `ZLINK_REGISTRY_STATE_DEGRADED` | 3 | Registry running with degraded connectivity |
| `ZLINK_REGISTRY_STATE_ERROR` | 4 | Registry in error state |

## Functions

### zlink_registry_new

Create a service registry.

```c
void *zlink_registry_new(void *ctx);
```

Allocates and initializes a new Registry instance. The Registry manages
internal PUB and ROUTER sockets for broadcasting and receiving registrations.
The context handle must remain valid for the lifetime of the Registry.

**Returns:** A Registry handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_bind`, `zlink_registry_destroy`

---

### zlink_registry_bind

Bind the Registry PUB and ROUTER endpoints and start the Registry.

```c
zlink_bind_result_t zlink_registry_bind(void *registry,
                                        const char *pub_endpoint,
                                        const char *router_endpoint);
```

Binds the Registry's PUB and ROUTER endpoints, verifies the bind succeeds,
starts the internal control task, and begins accepting registrations and
broadcasting the service list. The PUB endpoint is used for broadcasting to
Discovery instances. The ROUTER endpoint is used for receiving registration,
deregistration, and heartbeat messages from SPOT Nodes and socket family
services.

**Returns:** A `zlink_bind_result_t` value. Detailed internal errno remains
available through `zlink_errno()` for diagnostics.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call remains lifecycle-constrained and may be called at
most once per Registry.

**See also:** `zlink_registry_new`, `zlink_registry_destroy`

---

### zlink_registry_set_id

Set the registry unique ID.

```c
zlink_config_result_t zlink_registry_set_id(void *registry, uint32_t registry_id);
```

Assigns a unique identifier to this Registry instance. The ID is used for
cluster configuration when multiple registries synchronize with each other
via peer connections. Can be called before or after `zlink_registry_bind`;
changes take effect on the next runtime tick. Setting the ID after bind
may cause already-sent broadcasts to carry the previous value, so in
practice set it during setup.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe).

**See also:** `zlink_registry_add_peer`

---

### zlink_registry_add_peer

Add a peer registry PUB endpoint for cluster synchronization.

```c
zlink_config_result_t zlink_registry_add_peer(void *registry,
                                              const char *peer_pub_endpoint);
```

Connects this Registry to a peer Registry's PUB endpoint so that service
lists can be synchronized across a cluster. Multiple peers may be added.
Can be called before or after `zlink_registry_bind`; the runtime tick
picks up new peer endpoints and dials their PUB on the next cycle.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe).

**See also:** `zlink_registry_set_id`

---

### zlink_registry_set_heartbeat

Set the heartbeat interval and timeout.

```c
zlink_config_result_t zlink_registry_set_heartbeat(void *registry,
                                                   uint32_t interval_ms,
                                                   uint32_t timeout_ms);
```

Configures how frequently the Registry expects heartbeat messages from
registered services and when to consider a service expired. If a service
does not send a heartbeat within `timeout_ms` milliseconds, the Registry
removes it from the service list. Can be called at any time; the runtime
tick picks up the new values on the next cycle.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe).

**See also:** `zlink_registry_set_broadcast_interval`

---

### zlink_registry_set_broadcast_interval

Set the service list broadcast interval.

```c
zlink_config_result_t zlink_registry_set_broadcast_interval(void *registry,
                                                            uint32_t interval_ms);
```

Controls how frequently the Registry publishes the full service list on its
PUB socket. Discovery instances subscribed to the PUB endpoint will receive
updates at this interval. Can be called at any time; the runtime tick
picks up the new interval on the next cycle.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe).

**See also:** `zlink_registry_set_heartbeat`

---

### zlink_registry_destroy

Destroy the Registry and release all resources.

```c
zlink_close_result_t zlink_registry_destroy(void **registry_p);
```

Stops the internal thread, closes all sockets, and frees the Registry. The
pointer at `*registry_p` is set to `NULL` after destruction. If the Registry
was started, this function blocks until the internal thread exits.

**Returns:** A `zlink_close_result_t` value.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). `zlink_registry_destroy()` is more restrictive: if another
thread is executing an operational API on the same handle, destroy fails with
`errno=EBUSY`. A successful destroy clears `*registry_p`.

**See also:** `zlink_registry_new`, `zlink_registry_bind`

---

## Snapshot / Introspection

These APIs provide process-level health summaries and service-level
aggregate views of the Registry.

### Registry Status Snapshot

```c
zlink_config_result_t zlink_registry_status_snapshot(void *registry,
                                                     zlink_registry_status_t *out);
```

Returns a single-row process-level health summary of the Registry.

#### zlink_registry_status_t

```c
typedef struct zlink_registry_status_t
{
    uint32_t registry_id;
    char bind_endpoint[256];
    zlink_registry_state_t state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
} zlink_registry_status_t;
```

| Field | Description |
|-------|-------------|
| `registry_id` | Unique ID assigned via `zlink_registry_set_id()`. |
| `bind_endpoint` | Null-terminated bound endpoint. |
| `state` | `IDLE`, `ACTIVE`, `DEGRADED`, or `ERROR`. |
| `topology_entry_count` | Total entries in the topology table. |
| `peer_registry_count` | Configured peer registry count. |
| `connected_peer_registry_count` | Currently connected peer registries. |
| `list_seq` | Monotonic sequence number of the latest broadcast. |
| `last_error` | Last recorded error code, or 0. |
| `last_changed_ms` | Epoch ms of the last state change. |

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

---

### Registry Service Summary Snapshot

```c
zlink_config_result_t zlink_registry_service_summary_snapshot(
  void *registry,
  const zlink_registry_service_summary_filter_t *filter,
  zlink_registry_service_summary_entry_t *entries,
  size_t *count);
```

Returns service-level aggregate information. Each entry summarizes instance
counts by state for a given (service_kind, service_name) pair.

**Buffer convention:** Pass `entries = NULL` to query the required count.
Provide a caller-allocated buffer on the next call. If the buffer is too
small, the call returns `-1` with `errno = ENOBUFS` and `*count` set to the
needed capacity.

Results are ordered by (`service_kind`, `service_name`) ascending.

#### zlink_registry_service_summary_entry_t

```c
typedef struct zlink_registry_service_summary_entry_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
} zlink_registry_service_summary_entry_t;
```

| Field | Description |
|-------|-------------|
| `service_kind` | One of the `ZLINK_SERVICE_KIND_*` constants. |
| `service_role` | One of the `ZLINK_SERVICE_ROLE_*` constants. |
| `service_name` | Null-terminated service name. |
| `total_count` | Total registered instances for this service. |
| `connecting_count` | Instances currently connecting. |
| `ready_count` | Instances currently ready. |
| `error_count` | Instances in error state. |
| `stopped_count` | Instances that have stopped. |
| `last_reported_ms` | Epoch ms of the latest heartbeat across all instances. |

#### zlink_registry_service_summary_filter_t

```c
typedef struct zlink_registry_service_summary_filter_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
} zlink_registry_service_summary_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

---

## Topology & Query API

These APIs provide introspection into the global service topology managed
by the Registry.

### Topology Constants

```c
#define ZLINK_TOPOLOGY_SOURCE_MANUAL    1
#define ZLINK_TOPOLOGY_SOURCE_DISCOVERY 2
#define ZLINK_TOPOLOGY_SOURCE_REGISTRY  3

#define ZLINK_TOPOLOGY_STATE_DISCOVERED 1
#define ZLINK_TOPOLOGY_STATE_CONNECTING 2
#define ZLINK_TOPOLOGY_STATE_READY      3
#define ZLINK_TOPOLOGY_STATE_LOST       4
#define ZLINK_TOPOLOGY_STATE_ERROR      5
#define ZLINK_TOPOLOGY_STATE_STOPPED    6
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_TOPOLOGY_SOURCE_MANUAL` | 1 | Entry added via manual connect |
| `ZLINK_TOPOLOGY_SOURCE_DISCOVERY` | 2 | Entry discovered via Discovery |
| `ZLINK_TOPOLOGY_SOURCE_REGISTRY` | 3 | Entry registered via Registry |
| `ZLINK_TOPOLOGY_STATE_DISCOVERED` | 1 | Discovered but not yet connected |
| `ZLINK_TOPOLOGY_STATE_CONNECTING` | 2 | Connection in progress |
| `ZLINK_TOPOLOGY_STATE_READY` | 3 | Connected and ready |
| `ZLINK_TOPOLOGY_STATE_LOST` | 4 | Connection lost |
| `ZLINK_TOPOLOGY_STATE_ERROR` | 5 | Error state |
| `ZLINK_TOPOLOGY_STATE_STOPPED` | 6 | Stopped |

### Topology Types

#### zlink_registry_topology_entry_t

```c
typedef struct zlink_registry_topology_entry_t
{
    zlink_routing_id_t routing_id;
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    char endpoint[256];
    zlink_topology_source_t source;
    zlink_topology_state_t state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
} zlink_registry_topology_entry_t;
```

| Field | Description |
|-------|-------------|
| `routing_id` | Routing identity of the service instance. |
| `service_kind` | One of the `ZLINK_SERVICE_KIND_*` constants. |
| `service_role` | One of the `ZLINK_SERVICE_ROLE_*` constants. |
| `service_name` | Null-terminated service name. |
| `endpoint` | Null-terminated advertised endpoint. |
| `source` | How the entry was added (`ZLINK_TOPOLOGY_SOURCE_*`). |
| `state` | Current state (`ZLINK_TOPOLOGY_STATE_*`). |
| `desired_count` | Expected number of instances. |
| `ready_count` | Number of instances currently ready. |
| `error_code` | Error code if state is `ERROR`. |
| `last_reported_ms` | Timestamp (epoch ms) of the last heartbeat or update. |

#### zlink_registry_topology_filter_t

```c
typedef struct zlink_registry_topology_filter_t
{
    zlink_service_kind_t service_kind;
    zlink_service_role_t service_role;
    char service_name[256];
    zlink_routing_id_t routing_id;
    zlink_topology_state_t state;
    zlink_topology_source_t source;
} zlink_registry_topology_filter_t;
```

Set fields to non-zero values to filter by that criterion. Zero-valued
fields are treated as wildcards (match all).

---

### zlink_registry_topology_snapshot

Get a snapshot of the full topology from a local Registry instance.

```c
zlink_config_result_t zlink_registry_topology_snapshot(void *registry,
                                                       zlink_registry_topology_entry_t *entries,
                                                       size_t *count);
```

Fills `entries` with all registered services in the topology. On input
`*count` is the array capacity; on output it is the actual count. Pass
`entries = NULL` to query the required count first.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_topology_query`

---

### zlink_registry_topology_query

Query the local topology with a filter.

```c
zlink_config_result_t zlink_registry_topology_query(void *registry,
                                                    const zlink_registry_topology_filter_t *filter,
                                                    zlink_registry_topology_entry_t *entries,
                                                    size_t *count);
```

Like `zlink_registry_topology_snapshot` but only returns entries matching
the `filter` criteria.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_topology_snapshot`

---

### zlink_registry_query_client_new

Create a remote topology query client.

```c
void *zlink_registry_query_client_new(void *ctx);
```

Creates a client that can connect to a remote Registry and query its
topology. Use this when the Registry is running in a different process.

**Returns:** Query client handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_query_client_connect`, `zlink_registry_query_destroy`

---

### zlink_registry_query_client_connect

Connect the query client to a remote Registry.

```c
zlink_connect_result_t zlink_registry_query_client_connect(void *client,
                                                           const char *endpoint);
```

Connects to the Registry's ROUTER endpoint for topology queries.

**Returns:** A `zlink_connect_result_t` value.

**Thread safety:** Not thread-safe.

**See also:** `zlink_registry_query_snapshot`

---

### zlink_registry_query_snapshot

Query the remote Registry topology.

```c
zlink_config_result_t zlink_registry_query_snapshot(void *client,
                                                    const zlink_registry_topology_filter_t *filter,
                                                    zlink_registry_topology_entry_t *entries,
                                                    size_t *count);
```

Sends a topology query to the connected remote Registry and fills
`entries` with matching results. On input `*count` is the array capacity;
on output it is the actual count. Pass `filter = NULL` for an unfiltered
snapshot.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Not thread-safe.

**See also:** `zlink_registry_query_client_connect`

---

### zlink_registry_query_destroy

Destroy the query client and release resources.

```c
zlink_close_result_t zlink_registry_query_destroy(void **client_p);
```

Closes the client connection and sets `*client_p` to `NULL`.

**Returns:** A `zlink_close_result_t` value.

**Thread safety:** Not thread-safe.

**See also:** `zlink_registry_query_client_new`

---

## Member Peers API

These APIs provide introspection into the per-member peer state of
services managed by the Registry and Discovery.

### Member Peers Types

#### zlink_member_peer_entry_t

```c
typedef struct zlink_member_peer_entry_t
{
    zlink_auto_connect_type_t auto_connect_type;
    zlink_service_role_t service_role;
    char channel_name[256];
    char endpoint[256];
    uint32_t weight;
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;
```

| Field | Description |
|-------|-------------|
| `auto_connect_type` | Auto-connect channel type (`ZLINK_AUTO_CONNECT_*`). |
| `service_role` | Role of the service instance. |
| `channel_name` | Null-terminated channel name. |
| `endpoint` | Null-terminated endpoint. |
| `routing_id` | Routing identity of the peer. |
| `weight` | Current peer weight (`0..100`). `0` means the provider is excluded from new outbound candidate selection; positive values remain eligible and are selected proportionally. |
| `value` | Service-specific numeric value. |

---

### zlink_registry_member_peers

Get member peer entries for a service from a local Registry.

```c
zlink_config_result_t zlink_registry_member_peers(void *registry,
                                                  const char *channel_name,
                                                  zlink_member_peer_entry_t *entries,
                                                  size_t *count);
```

Fills `entries` with member peer entries matching the given channel name. On
input `*count` is the array capacity; on output it is the actual count. Pass
`entries = NULL` to query the required count first.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

---

### zlink_discovery_member_peers

Get member peer entries from a local Discovery instance.

```c
zlink_config_result_t zlink_discovery_member_peers(void *discovery,
                                                   zlink_member_peer_entry_t *entries,
                                                   size_t *count);
```

Fills `entries` with all member peer entries known to the Discovery
instance. On input `*count` is the array capacity; on output it is the
actual count. Pass `entries = NULL` to query the required count first.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

---

Route binding lookup is exposed on Discovery, not Registry. See
`zlink_discovery_bind_route`, `zlink_discovery_unbind_route`, and
`zlink_discovery_resolve_route` in the Discovery spec.
