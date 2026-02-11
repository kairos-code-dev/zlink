[English](receiver.md) | [한국어](receiver.ko.md)

# Receiver

The Receiver is the server-side counterpart to the Gateway. It receives
requests from Gateways, sends replies, and registers its services with the
Registry so that Gateways can discover and connect to it automatically.

## Constants

```c
#define ZLINK_RECEIVER_SOCKET_ROUTER 1
#define ZLINK_RECEIVER_SOCKET_DEALER 2
```

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_RECEIVER_SOCKET_ROUTER` | 1 | ROUTER socket used for receiving requests and sending replies |
| `ZLINK_RECEIVER_SOCKET_DEALER` | 2 | DEALER socket used for communication with the Registry |

## Functions

### zlink_receiver_new

Create a Receiver.

```c
void *zlink_receiver_new(void *ctx, const char *routing_id);
```

Allocates and initializes a new Receiver instance. The `routing_id`
uniquely identifies this Receiver to Gateways and the Registry. The context
handle must remain valid for the lifetime of the Receiver.

**Returns:** A Receiver handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_receiver_bind`, `zlink_receiver_register`, `zlink_receiver_destroy`

---

### zlink_receiver_bind

Bind the ROUTER socket to an endpoint.

```c
int zlink_receiver_bind(void *provider,
                        const char *bind_endpoint);
```

Binds the Receiver's internal ROUTER socket to the specified endpoint.
Gateways will connect to this endpoint to send requests. The endpoint is
typically a TCP address (e.g. `tcp://*:5555`).

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EADDRINUSE` -- the endpoint is already in use.

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_connect_registry

Connect to a Registry ROUTER endpoint.

```c
int zlink_receiver_connect_registry(void *provider,
                                    const char *registry_endpoint);
```

Connects the Receiver's internal DEALER socket to the Registry's ROUTER
endpoint. This connection is used for sending registration, deregistration,
heartbeat, and weight-update messages to the Registry.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_register

Register a service with the Registry.

```c
int zlink_receiver_register(void *provider,
                            const char *service_name,
                            const char *advertise_endpoint,
                            uint32_t weight);
```

Sends a registration request to the Registry for the given service name.
The `advertise_endpoint` is the endpoint that Gateways will connect to
(typically the same endpoint passed to `zlink_receiver_bind`). The `weight`
value is used by Gateways configured with weighted load balancing. A
Receiver may register multiple service names.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_unregister`, `zlink_receiver_update_weight`, `zlink_receiver_register_result`

---

### zlink_receiver_update_weight

Update the weight of a registered service.

```c
int zlink_receiver_update_weight(void *provider,
                                 const char *service_name,
                                 uint32_t weight);
```

Sends a weight-update message to the Registry for a previously registered
service. Gateways using weighted load balancing will reflect the new weight
after the next broadcast cycle.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_unregister

Unregister a service.

```c
int zlink_receiver_unregister(void *provider,
                              const char *service_name);
```

Sends a deregistration request to the Registry for the given service name.
After the next broadcast cycle, Gateways will no longer see this Receiver
for the specified service.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_register_result

Query the registration result.

```c
int zlink_receiver_register_result(void *provider,
                                   const char *service_name,
                                   int *status,
                                   char *resolved_endpoint,
                                   char *error_message);
```

Retrieves the asynchronous registration confirmation from the Registry for
the specified service name. The `status` output receives the registration
status code. The `resolved_endpoint` output (256-byte buffer) receives the
endpoint as resolved by the Registry. The `error_message` output (256-byte
buffer) receives a human-readable error description if the registration
failed.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_set_tls_server

Set TLS server certificate.

```c
int zlink_receiver_set_tls_server(void *provider,
                                  const char *cert,
                                  const char *key);
```

Configures the Receiver's ROUTER socket to use TLS with the given server
certificate and private key. The `cert` parameter is the path to the
certificate file and `key` is the path to the private key file. Must be
called before `zlink_receiver_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_gateway_set_tls_client`

---

### zlink_receiver_setsockopt

Set a socket option on an internal Receiver socket.

```c
int zlink_receiver_setsockopt(void *provider,
                              int socket_role,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

Applies a low-level socket option to one of the Receiver's internal sockets
identified by `socket_role`. Use `ZLINK_RECEIVER_SOCKET_ROUTER` for the
request/reply socket or `ZLINK_RECEIVER_SOCKET_DEALER` for the Registry
communication socket.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- invalid socket role or unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_bind`

---

### zlink_receiver_router

Return the internal ROUTER socket handle.

```c
void *zlink_receiver_router(void *provider);
```

Returns the raw ROUTER socket handle used internally by the Receiver. This
is intended for diagnostics and advanced use cases such as custom polling.
The caller must not close or modify the socket.

**Returns:** The ROUTER socket handle, or `NULL` if the Receiver is invalid.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_receiver_new`

---

### zlink_receiver_destroy

Destroy the Receiver and release all resources.

```c
int zlink_receiver_destroy(void **provider_p);
```

Closes all sockets, frees internal state, and releases the Receiver. The
pointer at `*provider_p` is set to `NULL` after destruction. Any registered
services are implicitly unregistered when the Receiver is destroyed.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Receiver operations.

**See also:** `zlink_receiver_new`
