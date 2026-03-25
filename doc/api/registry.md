[English](registry.md) | [한국어](registry.ko.md)

# Registry

## Current API Direction

- Registry remains the global service directory and topology summary source.
- Use `zlink_registry_topology_snapshot()` for local in-process summary access.
- Use `zlink_registry_query_client_*()` and `zlink_registry_query_snapshot()`
  for remote summary queries.
- Registry topology is intended for global summary only. For detailed local
  state transitions, use per-service monitor APIs.

The Registry is the central service directory for the zlink service layer. It
accepts service registration, deregistration, and heartbeat requests from
SPOT Nodes and socket family services, and periodically broadcasts the
aggregated service list to all connected Discovery instances.

## Constants

```c
typedef enum zlink_registry_socket_role_t
{
    ZLINK_REGISTRY_SOCKET_PUB      = 1,
    ZLINK_REGISTRY_SOCKET_ROUTER   = 2,
    ZLINK_REGISTRY_SOCKET_PEER_SUB = 3
} zlink_registry_socket_role_t;
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_REGISTRY_SOCKET_PUB` | 1 | PUB socket used for broadcasting the service list |
| `ZLINK_REGISTRY_SOCKET_ROUTER` | 2 | ROUTER socket used for receiving registrations and heartbeats |
| `ZLINK_REGISTRY_SOCKET_PEER_SUB` | 3 | SUB socket used for subscribing to peer registry broadcasts |

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
int zlink_registry_bind(void *registry,
                        const char *pub_endpoint,
                        const char *router_endpoint);
```

Binds the Registry's PUB and ROUTER endpoints, verifies the bind succeeds,
starts the internal control task, and begins accepting registrations and
broadcasting the service list. The PUB endpoint is used for broadcasting to
Discovery instances. The ROUTER endpoint is used for receiving registration,
deregistration, and heartbeat messages from SPOT Nodes and socket family
services.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call remains lifecycle-constrained and may be called at
most once per Registry.

**See also:** `zlink_registry_new`, `zlink_registry_destroy`

---

### zlink_registry_set_id

Set the registry unique ID.

```c
int zlink_registry_set_id(void *registry, uint32_t registry_id);
```

Assigns a unique identifier to this Registry instance. The ID is used for
cluster configuration when multiple registries synchronize with each other
via peer connections. Must be called before `zlink_registry_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call must still be made before `zlink_registry_bind`.

**See also:** `zlink_registry_add_peer`

---

### zlink_registry_add_peer

Add a peer registry PUB endpoint for cluster synchronization.

```c
int zlink_registry_add_peer(void *registry,
                            const char *peer_pub_endpoint);
```

Connects this Registry to a peer Registry's PUB endpoint so that service
lists can be synchronized across a cluster. Multiple peers may be added.
Must be called before `zlink_registry_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call must still be made before `zlink_registry_bind`.

**See also:** `zlink_registry_set_id`

---

### zlink_registry_set_heartbeat

Set the heartbeat interval and timeout.

```c
int zlink_registry_set_heartbeat(void *registry,
                                 uint32_t interval_ms,
                                 uint32_t timeout_ms);
```

Configures how frequently the Registry expects heartbeat messages from
registered services and when to consider a service expired. If a service
does not send a heartbeat within `timeout_ms` milliseconds, the Registry
removes it from the service list.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call must still be made before `zlink_registry_bind`.

**See also:** `zlink_registry_set_broadcast_interval`

---

### zlink_registry_set_broadcast_interval

Set the service list broadcast interval.

```c
int zlink_registry_set_broadcast_interval(void *registry,
                                          uint32_t interval_ms);
```

Controls how frequently the Registry publishes the full service list on its
PUB socket. Discovery instances subscribed to the PUB endpoint will receive
updates at this interval.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call must still be made before `zlink_registry_bind`.

**See also:** `zlink_registry_set_heartbeat`

---

### zlink_registry_setsockopt

Set a socket option on an internal Registry socket.

```c
int zlink_registry_setsockopt(void *registry,
                              zlink_registry_socket_role_t socket_role,
                              zlink_socket_option_t option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to one of the Registry's internal sockets
identified by `socket_role`. Use the `ZLINK_REGISTRY_SOCKET_*` constants to
select the target socket. Must be called before `zlink_registry_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** A single Registry handle can be used concurrently from
multiple threads (thread-safe). This call must still be made before `zlink_registry_bind`.

**See also:** `zlink_registry_bind`

---

### zlink_registry_destroy

Destroy the Registry and release all resources.

```c
int zlink_registry_destroy(void **registry_p);
```

Stops the internal thread, closes all sockets, and frees the Registry. The
pointer at `*registry_p` is set to `NULL` after destruction. If the Registry
was started, this function blocks until the internal thread exits.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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
int zlink_registry_status_snapshot(void *registry,
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

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### Registry Service Summary Snapshot

```c
int zlink_registry_service_summary_snapshot(
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
    char service_name[256];
} zlink_registry_service_summary_filter_t;
```

Set fields to non-zero values to filter. Zero-valued fields are wildcards.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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
int zlink_registry_topology_snapshot(void *registry,
                                      zlink_registry_topology_entry_t *entries,
                                      size_t *count);
```

Fills `entries` with all registered services in the topology. On input
`*count` is the array capacity; on output it is the actual count. Pass
`entries = NULL` to query the required count first.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_registry_topology_query`

---

### zlink_registry_topology_query

Query the local topology with a filter.

```c
int zlink_registry_topology_query(void *registry,
                                   const zlink_registry_topology_filter_t *filter,
                                   zlink_registry_topology_entry_t *entries,
                                   size_t *count);
```

Like `zlink_registry_topology_snapshot` but only returns entries matching
the `filter` criteria.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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
int zlink_registry_query_client_connect(void *client,
                                         const char *endpoint);
```

Connects to the Registry's ROUTER endpoint for topology queries.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_registry_query_snapshot`

---

### zlink_registry_query_snapshot

Query the remote Registry topology.

```c
int zlink_registry_query_snapshot(void *client,
                                   const zlink_registry_topology_filter_t *filter,
                                   zlink_registry_topology_entry_t *entries,
                                   size_t *count);
```

Sends a topology query to the connected remote Registry and fills
`entries` with matching results. On input `*count` is the array capacity;
on output it is the actual count. Pass `filter = NULL` for an unfiltered
snapshot.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_registry_query_client_connect`

---

### zlink_registry_query_destroy

Destroy the query client and release resources.

```c
int zlink_registry_query_destroy(void **client_p);
```

Closes the client connection and sets `*client_p` to `NULL`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

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
    zlink_service_type_t service_type;
    uint16_t service_role;
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    int64_t value;
} zlink_member_peer_entry_t;
```

| Field | Description |
|-------|-------------|
| `service_type` | Service type (`ZLINK_SERVICE_TYPE_*`). |
| `service_role` | Role of the service instance. |
| `service_name` | Null-terminated service name. |
| `endpoint` | Null-terminated endpoint. |
| `routing_id` | Routing identity of the peer. |
| `value` | Service-specific numeric value. |

---

### zlink_registry_member_peers

Get member peer entries for a service from a local Registry.

```c
int zlink_registry_member_peers(void *registry,
                                zlink_service_type_t service_type,
                                const char *service_name,
                                zlink_member_peer_entry_t *entries,
                                size_t *count);
```

Fills `entries` with member peer entries matching the given service type
and name. On input `*count` is the array capacity; on output it is the
actual count. Pass `entries = NULL` to query the required count first.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### zlink_registry_member_peer_metadata

Get metadata for a specific member peer from a local Registry.

```c
int zlink_registry_member_peer_metadata(void *registry,
                                        zlink_service_type_t service_type,
                                        const char *service_name,
                                        uint16_t service_role,
                                        const char *endpoint,
                                        zlink_msg_t *metadata_out);
```

Retrieves metadata for the member peer identified by service type, name,
role, and endpoint. The metadata is written into `metadata_out`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### zlink_discovery_member_peers

Get member peer entries from a local Discovery instance.

```c
int zlink_discovery_member_peers(void *discovery,
                                 zlink_member_peer_entry_t *entries,
                                 size_t *count);
```

Fills `entries` with all member peer entries known to the Discovery
instance. On input `*count` is the array capacity; on output it is the
actual count. Pass `entries = NULL` to query the required count first.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

---

### zlink_discovery_member_peer_metadata

Get metadata for a specific member peer from a local Discovery instance.

```c
int zlink_discovery_member_peer_metadata(void *discovery,
                                         uint16_t service_role,
                                         const char *endpoint,
                                         zlink_msg_t *metadata_out);
```

Retrieves metadata for the member peer identified by role and endpoint.
The metadata is written into `metadata_out`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.
