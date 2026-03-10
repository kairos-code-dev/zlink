[English](receiver.md) | [한국어](receiver.ko.md)

# Receiver

The Receiver is the server-side counterpart to the Gateway. It receives
requests from Gateways, sends replies, and registers its services with the
Registry so that Gateways can discover and connect to it automatically.

## Current API Direction

- Use `zlink_receiver_set_option()` for public service-level tuning.
- Use `zlink_receiver_set_routing_id()` / `zlink_receiver_routing_id()` for
  the representative Receiver identity.
- Use `zlink_receiver_monitor_open()` for state transitions such as
  `ZLINK_RECEIVER_REGISTER_OK`.
- Prefer `zlink_poller_add_receiver()` for data readiness and
  `zlink_poller_add_monitor()` for service monitor readiness.
- Treat `zlink_receiver_register_result()` as a low-level compatibility/debug
  surface rather than the primary public setup/readiness path.

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
int zlink_receiver_bind(void *receiver,
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
int zlink_receiver_connect_registry(void *receiver,
                                    const char *registry_endpoint);
```

Connects the Receiver's Discovery-owned control runtime to the Registry's
ROUTER endpoint. Registration, deregistration, and weight-update messages are
sent through that Discovery runtime. Receiver topology entries are reported as
Discovery-owned summaries rather than being derived directly from the control
path.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_register`

---

### zlink_receiver_register

Register a service with the Registry.

```c
int zlink_receiver_register(void *receiver,
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
int zlink_receiver_update_weight(void *receiver,
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
int zlink_receiver_unregister(void *receiver,
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
int zlink_receiver_register_result(void *receiver,
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
int zlink_receiver_set_tls_server(void *receiver,
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

### zlink_receiver_recv

Receive one multipart request from the Receiver service surface.

```c
int zlink_receiver_recv(void *receiver,
                        zlink_msg_t **parts,
                        size_t *part_count,
                        int flags,
                        zlink_routing_id_t *routing_id_out);
```

Receives a single request currently queued on the Receiver. The API returns
all frames as a multipart array and optionally reports the sender routing id.
Use `zlink_multipart_close()` to release `parts` after consumption.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EAGAIN` -- `ZLINK_DONTWAIT` was set and no request was ready.

**Thread safety:** Not thread-safe.

**See also:** `zlink_poller_add_receiver`, `zlink_multipart_close`

---

### zlink_receiver_last_endpoint

Resolve the bound endpoint for this Receiver.

```c
int zlink_receiver_last_endpoint(void *receiver,
                                 char *endpoint,
                                 size_t *size);
```

Returns the last effective bind endpoint of the Receiver's service socket.
This is the service-level replacement for reading `ZLINK_LAST_ENDPOINT`
through a raw internal ROUTER socket.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_bind`, `zlink_receiver_register`

---

### zlink_receiver_router_peers

Enumerate peer queue info from the Receiver ROUTER socket.

```c
int zlink_receiver_router_peers(void *receiver,
                                zlink_peer_info_t *peers,
                                size_t *count);
```

Returns peer-level queue stats (including send/receive pending message
counts) from the internal ROUTER socket. Use `peers = NULL` to query
required count first, then call again with an allocated array.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_socket_peers`, `zlink_receiver_peer_info`

---

### zlink_receiver_peer_info

Get peer info by routing identity from the Receiver ROUTER socket.

```c
int zlink_receiver_peer_info(void *receiver,
                              const zlink_routing_id_t *routing_id,
                              zlink_peer_info_t *info);
```

Looks up the peer identified by `routing_id` on the Receiver's internal
ROUTER socket and fills the `info` structure with its address, connection
time, and message counters.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- The routing identity was not found.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_receiver_router_peers`, `zlink_socket_peer_info`

---

### zlink_receiver_set_option

Set a Receiver service option.

```c
int zlink_receiver_set_option(void *receiver,
                               int option,
                               const void *optval,
                               size_t optvallen);
```

Applies a service-level option to the Receiver. Available option constants:

| Constant | Value | Description |
|----------|-------|-------------|
| `ZLINK_RECEIVER_OPT_SNDHWM` | 1 | Send high-water mark |
| `ZLINK_RECEIVER_OPT_RCVHWM` | 2 | Receive high-water mark |
| `ZLINK_RECEIVER_OPT_SNDTIMEO` | 3 | Send timeout (ms) |
| `ZLINK_RECEIVER_OPT_RCVTIMEO` | 4 | Receive timeout (ms) |
| `ZLINK_RECEIVER_OPT_LINGER` | 5 | Linger period (ms) |
| `ZLINK_RECEIVER_OPT_SNDBUF` | 6 | Kernel transmit buffer size in bytes |
| `ZLINK_RECEIVER_OPT_RCVBUF` | 7 | Kernel receive buffer size in bytes |

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Errors:**
- `EINVAL` -- Unknown option.

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_new`

---

### zlink_receiver_set_routing_id

Override the representative routing id before first use.

```c
int zlink_receiver_set_routing_id(void *receiver,
                                   const void *data,
                                   size_t size);
```

Sets a custom routing identity for this Receiver. Must be called before
`zlink_receiver_bind`.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe.

**See also:** `zlink_receiver_routing_id`

---

### zlink_receiver_routing_id

Return the representative routing id for this Receiver.

```c
int zlink_receiver_routing_id(void *receiver,
                               zlink_routing_id_t *out);
```

Retrieves the current routing identity of the Receiver.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_receiver_set_routing_id`

---

### zlink_receiver_destroy

Destroy the Receiver and release all resources.

```c
int zlink_receiver_destroy(void **receiver_p);
```

Closes all sockets, frees internal state, and releases the Receiver. The
pointer at `*receiver_p` is set to `NULL` after destruction. Any registered
services are implicitly unregistered when the Receiver is destroyed.

**Returns:** `0` on success, or `-1` on failure (errno is set).

**Thread safety:** Not thread-safe. Must not be called concurrently with other
Receiver operations.

**See also:** `zlink_receiver_new`
