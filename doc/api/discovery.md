[English](discovery.md) | [한국어](discovery.ko.md)

# Discovery

Discovery is a client-side cache that subscribes to Registry broadcasts and
maintains a local service directory. Applications use Discovery to look up
available Gateway peers or SPOT Nodes by service name without contacting the
Registry directly.

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

```c
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
    ZLINK_SERVICE_TYPE_SPOT    = 0x3002
} zlink_service_type_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_SERVICE_TYPE_GATEWAY` | Discovery type for Gateway services |
| `ZLINK_SERVICE_TYPE_SPOT` | Discovery type for SPOT Node services |

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
