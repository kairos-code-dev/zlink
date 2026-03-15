[English](discovery.md) | [한국어](discovery.ko.md)

# Discovery

Discovery is a client-side cache that subscribes to Registry broadcasts and
maintains a local service directory. Applications use Discovery to look up
available Receivers or SPOT Nodes by service name without contacting the
Registry directly.

## Thread-Safety Summary

Public Discovery handle APIs are thread-safe for same-handle operational use.
Not every call has the same timing constraints, though.

- `zlink_discovery_connect_registry()`, monitor operations, and query-style
  reads are valid at runtime.
- `zlink_discovery_set_routing_id()` is init-only in practice and only matters
  before the first subscribe/query/connect.
- `zlink_discovery_destroy()` uses a fail-fast lifecycle gate. If another
  thread is running a callback or admitted API on the same handle, destroy
  fails with `EBUSY`. Once destroy is accepted, new API entry fails with
  `ESHUTDOWN`.

## Current API Direction

- Use `zlink_discovery_set_routing_id()` / `zlink_discovery_routing_id()` for
  Discovery identity.
- Use `zlink_discovery_connect_registry()` as the single Registry bootstrap
  connect API. Discovery learns the broadcast and uplink paths internally.
- Use `zlink_discovery_monitor_open()` for state transitions such as
  `ZLINK_DISCOVERY_SERVICE_UP` and `ZLINK_DISCOVERY_PROVIDERS_CHANGED`.
- Use Registry topology snapshot/query APIs for global summary inspection.
- Discovery is not part of the new service-level option surface.

## Constants

```c
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
    ZLINK_SERVICE_TYPE_SPOT    = 0x3002
} zlink_service_type_t;

typedef enum zlink_discovery_socket_role_t
{
    ZLINK_DISCOVERY_SOCKET_SUB = 1
} zlink_discovery_socket_role_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_SERVICE_TYPE_GATEWAY` | Discovery type for Gateway services |
| `ZLINK_SERVICE_TYPE_SPOT` | Discovery type for SPOT Node services |
| `ZLINK_DISCOVERY_SOCKET_SUB` | SUB socket used for receiving Registry broadcasts |

## Functions

### zlink_discovery_new

Create a Discovery instance with a fixed service family.

```c
void *zlink_discovery_new (void *ctx, zlink_service_type_t service_type);
```

Allocates and initializes a new Discovery instance scoped to the given
service type. The type is fixed at creation time and cannot be changed. All
get and count queries operate within the specified service type scope. Use
`ZLINK_SERVICE_TYPE_GATEWAY` for Gateway services or
`ZLINK_SERVICE_TYPE_SPOT` for SPOT Node services.

**Returns:** A Discovery handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Connect to a Registry bootstrap/control endpoint.

```c
int zlink_discovery_connect_registry(void *discovery,
                                     const char *registry_endpoint);
```

Bootstraps this Discovery instance against the Registry control plane. The
Registry reply tells Discovery which internal broadcast and topology-uplink
endpoints to use. Discovery then configures those sockets automatically and
starts receiving periodic service list broadcasts.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Discovery is a control-plane subject in the tiered
contract. Same-handle calls remain thread-safe for correctness, but concurrent
control-path calls serialize internally rather than inheriting the hot-path
cost model.

**See also:** `zlink_discovery_destroy`

---

### zlink_discovery_setsockopt

Set a socket option on an internal Discovery socket.

```c
int zlink_discovery_setsockopt (
  void *discovery,
  zlink_discovery_socket_role_t socket_role,
  zlink_socket_option_t option,
  const void *optval,
  size_t optvallen);
```

Applies a low-level socket option to one of the Discovery's internal sockets.

**Returns:** `0` on success, or `-1` on failure (errno is set).

---

### zlink_discovery_set_routing_id

Override the representative routing id before first subscribe/query/connect.

```c
int zlink_discovery_set_routing_id (void *discovery,
                                    const void *data,
                                    size_t size);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_routing_id`

---

### zlink_discovery_routing_id

Return the representative routing id for this Discovery.

```c
int zlink_discovery_routing_id (void *discovery,
                                zlink_routing_id_t *out);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_set_routing_id`

---

### zlink_discovery_destroy

Destroy the Discovery instance and release all resources.

```c
int zlink_discovery_destroy(void **discovery_p);
```

Closes the internal SUB socket, frees all cached data, and releases the
Discovery instance. The pointer at `*discovery_p` is set to `NULL` after
destruction.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Discovery destroy uses the lifecycle gate. If another
thread is executing a Discovery callback or admitted API on the same handle,
destroy fails with `errno=EBUSY`. After destroy is accepted, new API entry
fails with `errno=ESHUTDOWN`. A successful destroy clears `*discovery_p`.

**See also:** `zlink_discovery_new`
