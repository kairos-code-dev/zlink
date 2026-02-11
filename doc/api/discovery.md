[English](discovery.md) | [한국어](discovery.ko.md)

# Discovery

Discovery is a client-side cache that subscribes to Registry broadcasts and
maintains a local service directory. Applications use Discovery to look up
available Receivers or SPOT Nodes by service name without contacting the
Registry directly.

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
#define ZLINK_SERVICE_TYPE_GATEWAY 1
#define ZLINK_SERVICE_TYPE_SPOT    2
#define ZLINK_DISCOVERY_SOCKET_SUB 1
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_SERVICE_TYPE_GATEWAY` | 1 | Discovery type for Gateway/Receiver services |
| `ZLINK_SERVICE_TYPE_SPOT` | 2 | Discovery type for SPOT Node services |
| `ZLINK_DISCOVERY_SOCKET_SUB` | 1 | SUB socket used for receiving Registry broadcasts |

## Functions

### zlink_discovery_new_typed

Create a typed Discovery instance.

```c
void *zlink_discovery_new_typed(void *ctx, uint16_t service_type);
```

Allocates and initializes a new Discovery instance scoped to the given
service type. The type is fixed at creation time and cannot be changed. All
subscribe, get, and count queries operate within the specified service type
scope. Use `ZLINK_SERVICE_TYPE_GATEWAY` for Gateway/Receiver services or
`ZLINK_SERVICE_TYPE_SPOT` for SPOT Node services.

**Returns:** A Discovery handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Connect to a Registry PUB endpoint.

```c
int zlink_discovery_connect_registry(void *discovery,
                                     const char *registry_pub_endpoint);
```

Subscribes this Discovery instance to the Registry's PUB socket so that it
receives periodic service list broadcasts. The Discovery cache is populated
automatically once broadcasts arrive.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Call before concurrent access begins.

**See also:** `zlink_discovery_subscribe`

---

### zlink_discovery_subscribe

Subscribe to a service name.

```c
int zlink_discovery_subscribe(void *discovery,
                              const char *service_name);
```

Registers interest in a particular service name. Only entries matching
subscribed service names are retained from Registry broadcasts. A Discovery
instance may subscribe to multiple service names.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_unsubscribe`, `zlink_discovery_get_receivers`

---

### zlink_discovery_unsubscribe

Unsubscribe from a service name.

```c
int zlink_discovery_unsubscribe(void *discovery,
                                const char *service_name);
```

Removes the subscription for the given service name. Cached entries for
this service are discarded and no further updates will be received for it.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_subscribe`

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

**See also:** `zlink_discovery_receiver_count`, `zlink_discovery_subscribe`

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
int zlink_discovery_setsockopt(void *discovery,
                               int socket_role,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Applies a low-level socket option to the Discovery's internal SUB socket
identified by `socket_role`. Use `ZLINK_DISCOVERY_SOCKET_SUB` as the socket
role.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_discovery_connect_registry`

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

**See also:** `zlink_discovery_new_typed`
