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
Receivers and SPOT Nodes, and periodically broadcasts the aggregated service
list to all connected Discovery instances.

## Constants

```c
#define ZLINK_REGISTRY_SOCKET_PUB      1
#define ZLINK_REGISTRY_SOCKET_ROUTER   2
#define ZLINK_REGISTRY_SOCKET_PEER_SUB 3
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

**See also:** `zlink_registry_set_endpoints`, `zlink_registry_start`, `zlink_registry_destroy`

---

### zlink_registry_set_endpoints

Set the Registry PUB and ROUTER endpoints.

```c
int zlink_registry_set_endpoints(void *registry,
                                 const char *pub_endpoint,
                                 const char *router_endpoint);
```

Configures the endpoints that the Registry will bind to. The PUB endpoint
is used for broadcasting the service list to Discovery instances. The ROUTER
endpoint is used for receiving registration, deregistration, and heartbeat
messages from Receivers and SPOT Nodes. Must be called before
`zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_new`, `zlink_registry_start`

---

### zlink_registry_set_id

Set the registry unique ID.

```c
int zlink_registry_set_id(void *registry, uint32_t registry_id);
```

Assigns a unique identifier to this Registry instance. The ID is used for
cluster configuration when multiple registries synchronize with each other
via peer connections. Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

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
Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

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

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

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

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_heartbeat`

---

### zlink_registry_setsockopt

Set a socket option on an internal Registry socket.

```c
int zlink_registry_setsockopt(void *registry,
                              int socket_role,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to one of the Registry's internal sockets
identified by `socket_role`. Use the `ZLINK_REGISTRY_SOCKET_*` constants to
select the target socket. Must be called before `zlink_registry_start`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** Not thread-safe. Must be called before `zlink_registry_start`.

**See also:** `zlink_registry_set_endpoints`

---

### zlink_registry_start

Start the Registry.

```c
int zlink_registry_start(void *registry);
```

Binds the configured endpoints, spawns an internal thread, and begins
accepting registrations and broadcasting the service list. All configuration
(endpoints, heartbeat, broadcast interval, socket options, peers) must be
set before calling this function.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must be called exactly once per Registry.

**See also:** `zlink_registry_set_endpoints`, `zlink_registry_destroy`

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

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Registry operations.

**See also:** `zlink_registry_new`

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
    uint16_t service_kind;
    char service_name[256];
    char endpoint[256];
    uint16_t source;
    uint16_t state;
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
    uint16_t service_kind;
    char service_name[256];
    zlink_routing_id_t routing_id;
    uint16_t state;
    uint16_t source;
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
