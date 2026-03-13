[English](discovery.md) | [한국어](discovery.ko.md)

# Discovery

Discovery is a client-side cache that subscribes to Registry broadcasts and
maintains a local service directory. Applications use Discovery to look up
available Receivers or SPOT Nodes by service name without contacting the
Registry directly.

## Current API Direction

- Use `zlink_discovery_set_routing_id()` / `zlink_discovery_routing_id()` for
  Discovery identity.
- Use `zlink_discovery_connect_registry()` as the single Registry bootstrap
  connect API. Discovery learns the broadcast and uplink paths internally.
- Use `zlink_discovery_monitor_open()` for state transitions such as
  `ZLINK_DISCOVERY_SERVICE_UP` and `ZLINK_DISCOVERY_PROVIDERS_CHANGED`.
- Use Registry topology snapshot/query APIs for global summary inspection.
- Discovery is not part of the new service-level option surface.

## Types

```c
typedef struct {
    char service_name[256];
    char endpoint[256];
    zlink_routing_id_t routing_id;
    uint32_t weight;
    uint64_t registered_at;
} zlink_receiver_info_t;
```

Each `zlink_receiver_info_t` describes a single registered service instance.
The `service_name` and `endpoint` fields identify the service. The
`routing_id` is the unique identifier assigned by the Receiver or SPOT Node.
The `weight` value is used for weighted load balancing, and `registered_at`
records the registration timestamp.

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

**Thread safety:** Not thread-safe. Call before concurrent access begins.

**See also:** `zlink_discovery_get_receivers`

---

### zlink_discovery_get_receivers

Get the list of receivers for a service.

```c
int zlink_discovery_get_receivers(void *discovery,
                                  const char *service_name,
                                  zlink_receiver_info_t *providers,
                                  size_t *count);
```

Copies the currently known receivers for `service_name` into the caller-
provided array. On input, `*count` specifies the array capacity. On output,
`*count` is set to the actual number of entries written. If the array is too
small, the function writes as many entries as fit and sets `*count` to the
number written.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_receiver_count`, `zlink_discovery_service_available`

---

### zlink_discovery_receiver_count

Return the number of registered receivers for a service.

```c
int zlink_discovery_receiver_count(void *discovery,
                                   const char *service_name);
```

Returns the count of receivers currently cached for the given service name.
This is a lightweight check that does not copy any data.

**Returns:** The receiver count on success (zero or positive), or `-1` on
failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_get_receivers`, `zlink_discovery_service_available`

---

### zlink_discovery_service_available

Check if a service is available.

```c
int zlink_discovery_service_available(void *discovery,
                                      const char *service_name);
```

Returns whether at least one receiver is registered for the given service
name. This is equivalent to checking if `zlink_discovery_receiver_count`
returns a value greater than zero, but expressed as a boolean result.

**Returns:** `1` if the service is available, `0` if not, or `-1` on failure
(errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_receiver_count`

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

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Discovery operations.

**See also:** `zlink_discovery_new`
