[English](discovery.md) | [한국어](discovery.ko.md)

# Discovery

Discovery is a client-side service view that subscribes to Registry broadcasts
and maintains a local service directory. Applications use Discovery to look up
available service providers by service name without contacting the Registry
directly. Discovery serves as the lifecycle owner for attached services --
SPOT Node and raw socket families (ROUTER/DEALER/PUB/SUB) all
delegate provider registration, peer refresh, and shutdown to their Discovery
instance.

## Thread-Safety Summary

A single Discovery handle can be used concurrently from multiple threads (thread-safe).
Not every call has the same timing constraints, though.

- `zlink_discovery_connect_registry()`, monitor operations, and query-style
  reads are valid at runtime.
- `zlink_set_routing_id()` is init-only in practice and only matters
  before the first subscribe/query/connect.
- `zlink_discovery_destroy()` uses a fail-fast lifecycle gate. If another
  thread is running a callback or admitted API on the same handle, destroy
  fails with `EBUSY`. Once destroy is accepted, new API entry fails with
  `ESHUTDOWN`.

## Current API Direction

- Use `zlink_set_routing_id(discovery, data, size)` /
  `zlink_get_routing_id(discovery, &out)` for Discovery identity.
- Use `zlink_set_tls_client(discovery, ca_cert, hostname, trust_system)` for
  TLS configuration on Discovery registry links.
- Use `zlink_discovery_connect_registry()` as the single Registry bootstrap
  connect API. Discovery learns the broadcast and uplink paths internally.
- Use `zlink_service_monitor_open(discovery, &options)` for state transitions
  such as `ZLINK_DISCOVERY_SERVICE_UP` and `ZLINK_DISCOVERY_PROVIDERS_CHANGED`.
  Close with `zlink_monitor_close()`.
- Use Registry topology snapshot/query APIs for global summary inspection.
- Discovery supports `zlink_set_option(discovery, ZLINK_OPT_*, ...)` which
  applies to its managed socket set as fan-out. No getter
  (`zlink_get_option`) is provided for Discovery (no single source-of-truth).

## Constants

### Service Types

```c
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_SPOT    = 0x3002,
    ZLINK_SERVICE_TYPE_SOCKET  = 0x3003
} zlink_service_type_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_SERVICE_TYPE_SPOT` | Discovery type for SPOT Node services |
| `ZLINK_SERVICE_TYPE_SOCKET` | Discovery type for raw socket families (ROUTER/DEALER/PUB/SUB) |

### Service Roles

```c
typedef enum zlink_service_role_t
{
    ZLINK_SERVICE_ROLE_INVALID = 0,
    ZLINK_SERVICE_ROLE_SPOT    = 2,
    ZLINK_SERVICE_ROLE_ROUTER  = 3,
    ZLINK_SERVICE_ROLE_DEALER  = 4,
    ZLINK_SERVICE_ROLE_PUB     = 5,
    ZLINK_SERVICE_ROLE_SUB     = 6
} zlink_service_role_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_SERVICE_ROLE_SPOT` | Fixed role for SPOT service type |
| `ZLINK_SERVICE_ROLE_ROUTER` | Socket family: ROUTER socket |
| `ZLINK_SERVICE_ROLE_DEALER` | Socket family: DEALER socket |
| `ZLINK_SERVICE_ROLE_PUB` | Socket family: PUB socket |
| `ZLINK_SERVICE_ROLE_SUB` | Socket family: SUB socket |

SPOT has a fixed role (automatically derived from its service type). Socket
family services require an explicit role matching the socket type. Role
matching rules: PUB pairs with SUB; ROUTER and DEALER pair with each other.

## Functions

### zlink_discovery_new

Create a Discovery instance with a fixed service view.

```c
void *zlink_discovery_new (void *ctx,
                           zlink_service_type_t service_type,
                           const char *service_name);
```

Allocates and initializes a new Discovery instance scoped to the given
service type and logical service name. Both are fixed at creation time and
cannot be changed. All subscribe/get/count queries operate within that one
logical service view.

Use `ZLINK_SERVICE_TYPE_SPOT` for SPOT Node services, or
`ZLINK_SERVICE_TYPE_SOCKET` for raw socket family services
(ROUTER/DEALER/PUB/SUB).

**Parameters:**
- `ctx` -- Context handle.
- `service_type` -- Service family for this handle.
- `service_name` -- Fixed logical service name for this handle.

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

### zlink_set_tls_client

Configure TLS settings for Discovery registry links.

```c
int zlink_set_tls_client (void *discovery,
                          const char *ca_cert,
                          const char *hostname,
                          int trust_system);
```

Applies TLS client configuration to the registry bootstrap and uplink
connections managed internally by the Discovery service. Must be called
before `zlink_discovery_connect_registry()`.

**Parameters:**
- `ca_cert` -- Path to PEM-encoded CA certificate bundle.
- `hostname` -- Expected hostname for TLS SNI and certificate verification.
- `trust_system` -- If non-zero, trust the system CA certificate store.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_connect_registry`

---

### zlink_set_routing_id

Override the representative routing id before first subscribe/query/connect.

```c
int zlink_set_routing_id (void *discovery,
                          const void *data,
                          size_t size);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_get_routing_id`

---

### zlink_get_routing_id

Return the representative routing id for this Discovery.

```c
int zlink_get_routing_id (void *discovery,
                          zlink_routing_id_t *out);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_set_routing_id`

---

### zlink_socket_attach_discovery

Attach a raw ROUTER/DEALER/PUB/SUB socket to a discovery service view.

```c
int zlink_socket_attach_discovery (void *socket, void *discovery);
```

Attaches the socket to the given Discovery instance. The Discovery service
type must be `ZLINK_SERVICE_TYPE_SOCKET` and the socket type must be one of
ROUTER, DEALER, PUB, or SUB. The service role is derived automatically from
the socket type.

Once attached, the socket delegates provider registration, peer refresh, and
shutdown to the Discovery instance. Manual `connect`, `disconnect`, `unbind`,
and `close` operations fail on attached sockets. Destroy the Discovery
instance to terminate the attached socket lifecycle.

**Parameters:**
- `socket` -- Socket handle (must be ROUTER, DEALER, PUB, or SUB).
- `discovery` -- Discovery handle created with `ZLINK_SERVICE_TYPE_SOCKET`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- Invalid socket or discovery handle.
- `ENOTSUP` -- Socket type not supported (must be ROUTER, DEALER, PUB, or SUB).
- `EBUSY` -- Socket already attached to a discovery instance, or has existing
  connect endpoints or attached pipes.

**Thread safety:** Same-handle calls remain thread-safe.

**See also:** `zlink_discovery_new`, `zlink_discovery_destroy`

---

### zlink_discovery_set_value

Set the numeric routing attribute for this Discovery instance.

```c
int zlink_discovery_set_value (void *discovery, int64_t value);
```

Sets the `value` field that is published alongside this service's
registration. Remote consumers see it in `zlink_member_peer_entry_t.value`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_get_value`, `zlink_discovery_set_metadata`

---

### zlink_discovery_get_value

Get the current numeric routing attribute.

```c
int zlink_discovery_get_value (void *discovery, int64_t *value_out);
```

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_set_value`

---

### zlink_discovery_set_metadata

Set the opaque metadata blob for this Discovery instance.

```c
int zlink_discovery_set_metadata (void *discovery,
                                   const void *data,
                                   size_t size);
```

Sets the opaque metadata blob published alongside this service's
registration. Remote consumers retrieve it via
`zlink_discovery_member_peer_metadata()` or
`zlink_registry_member_peer_metadata()`. Max size is runtime-configurable
(default 4 KiB); oversized blobs fail with `EMSGSIZE`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_get_metadata`, `zlink_discovery_set_value`

---

### zlink_discovery_get_metadata

Get the current metadata blob.

```c
int zlink_discovery_get_metadata (void *discovery,
                                   zlink_msg_t *metadata_out);
```

Copies the current metadata into `metadata_out`. The caller must
initialize the message before the call and close it after use.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**See also:** `zlink_discovery_set_metadata`

---

### zlink_discovery_destroy

Destroy the Discovery instance and release all resources.

```c
int zlink_discovery_destroy(void **discovery_p);
```

Closes the internal SUB socket, frees all cached data, and releases the
Discovery instance. Destroying a Discovery also shuts down every attached
service participant (SPOT Node or socket) that delegated lifecycle
ownership to this service view. The pointer at `*discovery_p` is set to
`NULL` after destruction.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Discovery destroy uses the lifecycle gate. If another
thread is executing a Discovery callback or admitted API on the same handle,
destroy fails with `errno=EBUSY`. After destroy is accepted, new API entry
fails with `errno=ESHUTDOWN`. A successful destroy clears `*discovery_p`.

**See also:** `zlink_discovery_new`
