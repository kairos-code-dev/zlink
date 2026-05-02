[English](discovery.md) | [한국어](discovery.ko.md)

[Spec Index](../../README.md) · [Core Index](../README.md) · [Service Common](README.md)

# Discovery

Discovery is the handle that keeps Registry-provided service information close
to the caller. Applications use it to ask simple questions such as "which
providers are currently available" or "can I send to this logical name right
now" without talking to Registry on every operation. Discovery can also act as
the lifecycle coordination point for attached services. SPOT Node and raw
socket families (ROUTER/DEALER/PUB/SUB) may delegate registration, refresh,
and shutdown to the Discovery instance they are attached to.

## SPOT address lookup and cache

In a managed SPOT configuration, Discovery does not own the final answer for
`spot_rid` routing. Registry owns that final answer. Discovery keeps nearby
results so the send path can answer quickly.

- Registry ownership authority and handover rules:
  [registry.md](registry.md)
- SPOT direct submit public API contract:
  [spot.md](spot.md)

The key job for Discovery is simple: inside the current Discovery
`channel_name` view, given a `spot_rid`, answer which `SpotNode` currently
owns it. If a fresh local answer is available, Discovery may return it
immediately. If not, it refreshes against Registry.

This document version exposes that lookup through the following public API.

```c
zlink_config_result_t zlink_discovery_resolve_spot (
  void *discovery,
  const zlink_routing_id_t *spot_rid,
  zlink_routing_id_t *owner_node_rid_out);
```

On success, the caller combines `owner_node_rid_out` with the original
`spot_rid` and passes them to the ROUTER-side direct functions
(`zlink_router_send_spot()` or `zlink_router_request_spot()`). That
lookup result is scoped to the current Discovery `channel_name`.

### Cache model

The Discovery address cache is not a full replicated copy of the entire
ownership table.

- Discovery may operate as a hot cache that keeps only the subset of ownership
  entries it recently resolved or frequently uses.
- A cache entry must preserve both `spot_rid -> owner_node_rid` and the
  ordering token.
- A newer ownership update must replace the previous cache entry immediately.
- An older ownership update must be ignored.
- A withdrawn or tombstone update must remove the active owner from cache.

A missing cache entry does not by itself mean the address does not exist.
Discovery must be able to ask Registry again after a cache miss.

### Scale-out assumptions

This design assumes a high-cardinality environment. The implementation should
allow logical `spot_rid` populations on the order of 10,000 nodes with 10,000
spots per node, or similarly large totals.

- The implementation must not assume that every Discovery instance always
  holds every `spot_rid` ownership entry in memory.
- Ownership refresh and liveness maintenance must be able to work with
  aggregated mechanisms such as node-session heartbeat, batched refresh, or
  lease renewal rather than requiring a per-spot heartbeat.

### Resolve order

A caller that starts from only `spot_rid` obtains the final
`dest_node_rid + dest_spot_rid` pair inside the current Discovery
`channel_name` in the following order.

1. Look up `spot_rid` ownership in the local Discovery cache.
2. If an active owner exists, normalize it into the
   `dest_node_rid + dest_spot_rid` pair.
3. On cache miss or inactive / withdrawn state, perform a Registry lookup or
   refresh.
4. If no owner exists after refresh, fail as destination-not-found.
5. After submit, if route miss, stale-owner mismatch, or ownership handover is
   detected, one refresh-and-resolve retry may be attempted.

That retry is best-effort. Unbounded retry is not part of the contract.

The Registry-side refresh may target a local shard, a remote shard, or an
equivalent authority service.

### Local fast path and in-flight requests during handover

If the resolved `owner_node_rid` is the current node, the implementation may
use a local fast path. The external contract does not change. Final submit is
interpreted as using the normalized
`dest_node_rid + dest_spot_rid` pair and the routed path.

If a request has already been delivered to a specific owner pair, that request
continues on the already resolved path even if ownership handover happens
while it is being processed. Only requests resolved after the handover use the
new authoritative owner.

## Auto-Connect Policy

For Discovery-attached channels, the current Discovery `channel_name` is the
auto-connect boundary. Managed auto-connect operates only inside that channel
scope and never crosses into a different `channel_name`.

### SpotNode Discovery attach

`zlink_spot_node_attach_discovery()` accepts only
`ZLINK_AUTO_CONNECT_SPOT_MESH` Discovery handles. This Discovery provides the
SPOT channel view that determines the node's mesh auto-connect scope.

- A node may have at most one active SPOT Discovery view.
- A second SPOT Discovery attach is rejected with `EBUSY`.
- Destroying the attached Discovery removes the automatic peer set it
  supplied.

### SpotNode channel dealer attach

To call another channel from a `SpotNode`, the caller attaches a `DEALER`
via `zlink_spot_node_attach_channel_dealer()`. This function takes a
`ZLINK_AUTO_CONNECT_CLIENT_SERVER` Discovery together with the `DEALER` socket.
The Discovery manages the peer set for that channel.

- A Discovery has exactly one fixed `channel_name` (channel) view.
- The same `channel_name` may have at most one `DEALER` (automatic and
  manual attach combined). Duplicates fail with `EBUSY`.
- The same Discovery handle must not be attached to more than one owner.
- Attached dealers are dedicated to the `SpotNode`. The caller keeps
  ownership, but the socket must not be reused elsewhere.
- For manual channel dealer attach without Discovery, use
  `zlink_spot_node_attach_channel_dealer_manual()`.

### SPOT Node

SPOT Node may automatically discover and connect to other SPOT Node endpoints
that belong to the same `channel_name`, excluding its own advertised endpoint.

- Only SPOT Node endpoints from the same `channel_name` are candidates.
- A node must not auto-connect to its own advertised endpoint.
- Manual peer connect/disconnect and Discovery-managed auto-connect must not
  be mixed.

### Raw socket family

Raw socket family auto-connect follows role-directed rules. These rules are
not just "which roles are compatible"; they define which side is allowed to
initiate the outbound connect.

- `ZLINK_AUTO_CONNECT_ROUTE_MESH`: ROUTER peers form a mesh, with one
  initiator per pair.
- `ZLINK_AUTO_CONNECT_CLIENT_SERVER`: DEALER peers connect to every eligible
  ROUTER endpoint in the same channel.
- `ZLINK_AUTO_CONNECT_DEALER_MESH`: DEALER peers form a mesh, with one
  initiator per pair.
- `ZLINK_AUTO_CONNECT_FANOUT`: SUB peers connect to PUB endpoints.
- `ZLINK_AUTO_CONNECT_SPOT_MESH`: SpotNode peers form a mesh, with one
  initiator per pair.

### Pairwise initiator rule

For `ROUTE_MESH`, `DEALER_MESH`, and `SPOT_MESH`, a single successful connect
already provides a bidirectional message path. Letting both sides dial in
parallel creates duplicate-connection races and handover churn, so the library
decides internally that exactly one side of each pair initiates the connect.

- The comparison key is `routing_id` (primary) with the advertised endpoint
  string as a tie-break. Both peers compute the same total order from the
  same inputs, so each pair has exactly one initiator.
- Users do not configure who-dials-whom; the externally observable behavior
  is "only one side dials."
- The rule applies to Discovery-managed auto-connect only. Manual
  `zlink_connect()` calls made through the raw API are not mediated by the
  library; the caller remains responsible for connection direction.

### Auto-connected peer entries and weight

Peer entries surfaced by Discovery carry peer weight.
`zlink_member_peer_entry_t.weight` stores the current `0..100` value for each
peer. DEALER attachments exclude peers with weight `0` from candidate
selection and fail submit with `ZLINK_SUBMIT_NOT_ADMITTED` when every known
peer has weight `0`. Raw ROUTER and DEALER sockets are the public handles that
can change local advertised weight.

There is no runtime setter for a DEALER target policy. Select
`ZLINK_AUTO_CONNECT_CLIENT_SERVER` for DEALER-to-ROUTER client/server channels,
or `ZLINK_AUTO_CONNECT_DEALER_MESH` for DEALER-to-DEALER mesh channels when the
Discovery handle is created.

## Thread-Safety Summary

A single Discovery handle can be used concurrently from multiple threads (thread-safe).
Not every call has the same timing constraints, though.

- `zlink_discovery_connect_registry()`, `zlink_discovery_resolve_spot()`,
  monitor operations, and query-style reads are valid at runtime.
- `zlink_set_routing_id()` is init-only in practice and only matters
  before the first subscribe/query/connect.
- `zlink_discovery_destroy()` uses a fail-fast lifecycle gate. If another
  thread is running a callback or admitted API on the same handle, destroy
  fails with `EBUSY`. Once destroy is accepted, new API entry fails with
  `ESHUTDOWN`.

## API Surface

- Use `zlink_set_routing_id(discovery, data, size)` /
  `zlink_get_routing_id(discovery, &out)` for Discovery identity.
- Use `zlink_set_tls_client(discovery, ca_cert, hostname, trust_system)` for
  TLS configuration on Discovery registry links.
- Use `zlink_discovery_connect_registry()` as the single Registry bootstrap
  connect API. Discovery learns the broadcast and uplink paths internally.
- Use `zlink_discovery_resolve_spot()` when the caller starts from a logical
  `spot_rid` and needs the current destination `node_rid`.
- Use `zlink_discovery_member_peers()` and
  `zlink_discovery_member_peer_metadata()` for the current Discovery view.
  When the caller needs a stable service-level picture, poll these query
  functions and compare snapshots over time.
- Use Registry topology snapshot/query APIs for global summary inspection.
- Discovery supports `zlink_set_option(discovery, ZLINK_OPT_*, ...)` which
  applies to its managed socket set as fan-out. No getter
  (`zlink_get_option`) is provided for Discovery (no single source-of-truth).

## Constants

### Auto-Connect Types

```c
typedef enum zlink_auto_connect_type_t
{
    ZLINK_AUTO_CONNECT_INVALID = 0,
    ZLINK_AUTO_CONNECT_ROUTE_MESH = 1,
    ZLINK_AUTO_CONNECT_CLIENT_SERVER = 2,
    ZLINK_AUTO_CONNECT_DEALER_MESH = 3,
    ZLINK_AUTO_CONNECT_FANOUT = 4,
    ZLINK_AUTO_CONNECT_SPOT_MESH = 5
} zlink_auto_connect_type_t;
```

| Constant | Description |
|----------|-------------|
| `ZLINK_AUTO_CONNECT_ROUTE_MESH` | ROUTER mesh channel |
| `ZLINK_AUTO_CONNECT_CLIENT_SERVER` | DEALER clients to ROUTER servers |
| `ZLINK_AUTO_CONNECT_DEALER_MESH` | DEALER mesh channel |
| `ZLINK_AUTO_CONNECT_FANOUT` | PUB to SUB fanout channel |
| `ZLINK_AUTO_CONNECT_SPOT_MESH` | SpotNode mesh channel |

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
| `ZLINK_SERVICE_ROLE_SPOT` | Fixed SPOT role |
| `ZLINK_SERVICE_ROLE_ROUTER` | Socket family: ROUTER socket |
| `ZLINK_SERVICE_ROLE_DEALER` | Socket family: DEALER socket |
| `ZLINK_SERVICE_ROLE_PUB` | Socket family: PUB socket |
| `ZLINK_SERVICE_ROLE_SUB` | Socket family: SUB socket |

SPOT has a fixed role. Socket family services derive their role from the
attached socket type. Auto-connect then follows the channel's fixed
`zlink_auto_connect_type_t` contract.

## Functions

### zlink_discovery_new

Create a Discovery instance with a fixed channel view.

```c
void *zlink_discovery_new (void *ctx,
                           zlink_auto_connect_type_t auto_connect_type,
                           const char *channel_name);
```

Allocates and initializes a new Discovery instance scoped to the given
auto-connect type and logical channel name. Both are fixed at creation time and
cannot be changed. All subscribe/get/count queries operate within that one
logical channel view.

**Parameters:**
- `ctx` -- Context handle.
- `auto_connect_type` -- Auto-connect topology contract for this handle.
- `channel_name` -- Fixed logical channel name for this handle.

**Returns:** A Discovery handle on success, or `NULL` on failure.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_connect_registry`, `zlink_discovery_destroy`

---

### zlink_discovery_connect_registry

Connect to a Registry bootstrap/control endpoint.

```c
zlink_connect_result_t zlink_discovery_connect_registry(void *discovery,
                                                        const char *registry_endpoint);
```

Bootstraps this Discovery instance against the Registry control plane. The
Registry reply tells Discovery which internal broadcast and topology-uplink
endpoints to use. Discovery then configures those sockets automatically and
starts receiving periodic service list broadcasts.

**Returns:** A `zlink_connect_result_t` value. Detailed internal errno remains
available through `zlink_errno()` for diagnostics.

**Thread safety:** Discovery is a control-plane subject in the tiered
contract. Same-handle calls remain thread-safe for correctness, but concurrent
control-path calls serialize internally rather than inheriting the hot-path
cost model.

**See also:** `zlink_discovery_resolve_spot`, `zlink_discovery_destroy`

---

### zlink_discovery_resolve_spot

Resolve which `SpotNode` currently owns a logical `spot_rid`.

```c
zlink_config_result_t zlink_discovery_resolve_spot (void *discovery,
                                                    const zlink_routing_id_t *spot_rid,
                                                    zlink_routing_id_t *owner_node_rid_out);
```

This function accepts a logical `spot_rid` inside the current Discovery
`channel_name` view and returns the `node_rid` of the `SpotNode` that
currently owns that name. Discovery may answer from local cache first and
refresh against Registry when needed.

The current core implementation does not trust that cache indefinitely.
Discovery first checks whether the cached owner row was validated against the
current service-view update sequence. If not, it reuses the cached row only for
a short local TTL. Once the channel view changes or that short TTL expires,
Discovery queries Registry again before returning the owner.

On success, `owner_node_rid_out` receives the current owner node routing id.
The caller then combines that node id with the original `spot_rid` and passes
them to the ROUTER-side direct functions (`zlink_router_send_spot()` or
`zlink_router_request_spot()`).

This function is for send/request destination lookup. It is not used for
reply. Reply paths must use the concrete source address that came with the
incoming request.

**Returns:** A `zlink_config_result_t` value. The call may fail if no current
owner exists or if Discovery cannot reach current Registry-backed ownership
information. Detailed errno remains available through `zlink_errno()` for
diagnostics.

**Thread safety:** Safe to call concurrently on the same Discovery handle
subject to the normal runtime lifecycle constraints.

**See also:** `zlink_router_send_spot`, `zlink_router_request_spot`

---

### Removed dealer target setter

There is no `zlink_discovery_set_dealer_peer_mode()` API. Use
`ZLINK_AUTO_CONNECT_CLIENT_SERVER` for DEALER-to-ROUTER channels and
`ZLINK_AUTO_CONNECT_DEALER_MESH` for DEALER-to-DEALER channels.

**See also:** `zlink_discovery_connect_registry`,
`zlink_socket_attach_discovery`

---

### zlink_set_tls_client

Configure TLS settings for Discovery registry links.

```c
zlink_config_result_t zlink_set_tls_client (void *discovery,
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

**Returns:** A `zlink_config_result_t` value.

**See also:** `zlink_discovery_connect_registry`

---

### zlink_set_routing_id

Override the representative routing id before first subscribe/query/connect.

```c
zlink_config_result_t zlink_set_routing_id (void *discovery,
                                            const void *data,
                                            size_t size);
```

**Returns:** A `zlink_config_result_t` value.

**See also:** `zlink_get_routing_id`

---

### zlink_get_routing_id

Return the representative routing id for this Discovery.

```c
zlink_config_result_t zlink_get_routing_id (void *discovery,
                                            zlink_routing_id_t *out);
```

**Returns:** A `zlink_config_result_t` value.

**See also:** `zlink_set_routing_id`

---

### zlink_socket_attach_discovery

Attach a raw ROUTER/DEALER/PUB/SUB socket to a discovery channel view.

```c
zlink_config_result_t zlink_socket_attach_discovery (void *socket, void *discovery);
```

Attaches the socket to the given Discovery instance. The Discovery service
type must be `ZLINK_AUTO_CONNECT_CLIENT_SERVER` and the socket type must be one of
ROUTER, DEALER, PUB, or SUB. The service role is derived automatically from
the socket type.

Once attached, the socket delegates provider registration, peer refresh, and
shutdown to the Discovery instance. Manual `connect`, `disconnect`, `unbind`,
and `close` operations fail on attached sockets. Destroy the Discovery
instance to terminate the attached socket lifecycle.

**Parameters:**
- `socket` -- Socket handle (must be ROUTER, DEALER, PUB, or SUB).
- `discovery` -- Discovery handle created with `ZLINK_AUTO_CONNECT_CLIENT_SERVER`.

**Returns:** A `zlink_config_result_t` value.

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
zlink_config_result_t zlink_discovery_set_value (void *discovery, int64_t value);
```

Sets the `value` field that is published alongside this service's
registration. Remote consumers see it in `zlink_member_peer_entry_t.value`.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_get_value`, `zlink_discovery_set_metadata`

---

### zlink_discovery_get_value

Get the current numeric routing attribute.

```c
zlink_config_result_t zlink_discovery_get_value (void *discovery,
                                                 int64_t *value_out);
```

**Returns:** A `zlink_config_result_t` value.

**See also:** `zlink_discovery_set_value`

---

### zlink_discovery_set_metadata

Set the opaque metadata blob for this Discovery instance.

```c
zlink_config_result_t zlink_discovery_set_metadata (void *discovery,
                                                    const void *data,
                                                    size_t size);
```

Sets the opaque metadata blob published alongside this service's
registration. Remote consumers retrieve it via
`zlink_discovery_member_peer_metadata()` or
`zlink_registry_member_peer_metadata()`. Max size is runtime-configurable
(default 4 KiB); oversized blobs fail with `EMSGSIZE`.

**Returns:** A `zlink_config_result_t` value.

**Thread safety:** Safe to call from any thread.

**See also:** `zlink_discovery_get_metadata`, `zlink_discovery_set_value`

---

### zlink_discovery_get_metadata

Get the current metadata blob.

```c
zlink_config_result_t zlink_discovery_get_metadata (void *discovery,
                                                    zlink_msg_t *metadata_out);
```

Copies the current metadata into `metadata_out`. The caller must
initialize the message before the call and close it after use.

**Returns:** A `zlink_config_result_t` value.

**See also:** `zlink_discovery_set_metadata`

---

### zlink_discovery_destroy

Destroy the Discovery instance and release all resources.

```c
zlink_close_result_t zlink_discovery_destroy(void **discovery_p);
```

Closes the internal SUB socket, frees all cached data, and releases the
Discovery instance. Destroying a Discovery also shuts down every attached
service participant (SPOT Node or socket) that delegated lifecycle
ownership to this channel view. The pointer at `*discovery_p` is set to
`NULL` after destruction.

**Returns:** A `zlink_close_result_t` value.

**Thread safety:** Discovery destroy uses the lifecycle gate. If another
thread is executing a Discovery callback or admitted API on the same handle,
destroy fails with `errno=EBUSY`. After destroy is accepted, new API entry
fails with `errno=ESHUTDOWN`. A successful destroy clears `*discovery_p`.

**See also:** `zlink_discovery_new`
