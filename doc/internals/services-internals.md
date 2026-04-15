[English](services-internals.md) | [한국어](services-internals.ko.md)

# Service Layer Internal Design

## 1. Overview

The zlink service layer provides two high-level services: Discovery and SPOT. This document covers the internal implementation details.

For SPOT, transport-security ownership is intentionally narrow: the
`SpotNode` owns TLS/WSS wiring for mesh/control sockets, while unified
`Spot` remains a borrowed data-plane facade only. The facade never owns
node lifecycle and is not itself a TLS configuration surface.

## 2. Registry Internal Implementation

### 2.1 Data Structures

```cpp
struct service_entry_t {
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint16_t service_role;
    uint64_t registered_at;
    uint64_t last_heartbeat;
    uint32_t weight;
};

struct registry_state_t {
    uint32_t registry_id;
    uint64_t list_seq;
    std::map<std::string, std::vector<service_entry_t>> services;
};
```

### 2.2 Registry State Machine

```mermaid
stateDiagram-v2
    [*] --> INIT
    INIT --> RUNNING : start()
    RUNNING --> STOPPED : stop()
    STOPPED --> [*]
```

### 2.3 SERVICE_LIST Broadcast Triggers
| Trigger | Description |
|--------|------|
| Registration | After successful service REGISTER |
| Deregistration | UNREGISTER or Heartbeat timeout |
| Periodic | 30 seconds (default, configurable) |

### 2.4 Cluster Synchronization
- Each Registry subscribes to other Registries' PUB via SUB
- Immediate propagation via flooding
- Duplicates/reversals ignored using registry_id + list_seq

## 3. Discovery Internal Implementation

### 3.1 State Machine (Per Service)

```mermaid
stateDiagram-v2
    [*] --> EMPTY
    EMPTY --> AVAILABLE : SERVICE_LIST (count > 0)
    AVAILABLE --> UNAVAILABLE : SERVICE_LIST (count == 0)
    UNAVAILABLE --> AVAILABLE : SERVICE_LIST (count > 0)
```

### 3.2 Service Types and Roles

Discovery tracks providers with a (service_type, service_role) pair:

```cpp
// Service types
static const uint16_t service_type_spot_node = 2;
static const uint16_t service_type_socket = 3;

// Service roles
enum service_role_t {
    service_role_invalid = 0,
    service_role_spot    = 2,  // fixed for spot type
    service_role_router  = 3,  // socket family
    service_role_dealer  = 4,  // socket family
    service_role_pub     = 5,  // socket family
    service_role_sub     = 6   // socket family
};
```

SPOT has a fixed role derived from its service type. Socket family
services require an explicit role matching the socket type. Role
matching rules for peer discovery:
- PUB ↔ SUB
- ROUTER ↔ ROUTER, ROUTER ↔ DEALER, DEALER ↔ DEALER
- SPOT ↔ SPOT

### 3.3 Discovery-Owned Service Execution

Discovery acts as the lifecycle owner for attached services. Each service
type registers its endpoint(s) through the `discovery_owned_service`
convenience API:

```cpp
namespace discovery_owned_service {
    int register_endpoint(discovery_t *, uint16_t service_type,
                          const char *endpoint, uint32_t weight,
                          std::string *resolved_endpoint_out,
                          const zlink_routing_id_t *routing_id = NULL,
                          uint16_t service_role = 0);
    int update_weight(discovery_t *, uint16_t service_type,
                      const char *endpoint, uint32_t weight,
                      uint16_t service_role = 0);
    int unregister_endpoint(discovery_t *, uint16_t service_type,
                            const char *endpoint,
                            uint16_t service_role = 0);
}
```

Discovery internally maintains a `_registered_services` map keyed by
`(service_type, service_role, service_name, endpoint)` and periodically
refreshes heartbeats for all registered services via
`refresh_registered_service_heartbeats()`.

### 3.4 Socket Discovery Attachment

`socket_discovery_attachment_t` integrates raw socket lifecycle with
Discovery. When a socket is attached:

1. Validates the socket type is supported (ROUTER/DEALER/PUB/SUB)
2. Derives the service role from the socket type
3. Registers the socket's bound endpoint via `discovery_owned_service`
4. Observes service list updates and refreshes peer connections
5. Reports topology state changes back to Discovery
6. Blocks manual connect/disconnect/unbind/close operations

### 3.5 Subscription Behavior
- Subscribes to all Registry PUB (no network-level filtering)
- subscribe/unsubscribe operate as internal filters

### 3.6 Duplicate/Reversal Handling
- Applies only the latest snapshot based on (registry_id, list_seq)
- Ignores earlier list_seq from the same registry_id

## 4. Message Protocol

### 4.1 Frame Structure
```
Frame 0: msgId (uint16_t)
Frame 1~N: Payload (variable)
```

### 4.2 Message Types
| msgId | Name | Direction |
|-------|------|------|
| 0x0001 | REGISTER | Service → Registry |
| 0x0002 | REGISTER_ACK | Registry → Service |
| 0x0003 | UNREGISTER | Service → Registry |
| 0x0004 | HEARTBEAT | Service → Registry |
| 0x0005 | SERVICE_LIST | Registry → Discovery |
| 0x0006 | REGISTRY_SYNC | Registry → Registry |
| 0x0007 | UPDATE_ATTRIBUTES | Service → Registry |
| 0x0008 | BOOTSTRAP_REQ | Discovery → Registry |
| 0x0009 | BOOTSTRAP_REP | Registry → Discovery |
| 0x000A | TOPOLOGY_REPORT | Discovery → Registry |
| 0x000B | TOPOLOGY_QUERY | Client → Registry |
| 0x000C | TOPOLOGY_REPLY | Registry → Client |
| 0x000D | UNREGISTER_ACK | Registry → Service |

#### Registration and Heartbeat Flow

```mermaid
sequenceDiagram
    participant S as Service
    participant R as Registry
    participant D as Discovery

    S->>R: REGISTER
    R->>S: REGISTER_ACK
    loop Every heartbeat interval
        S->>R: HEARTBEAT
    end
    R->>D: SERVICE_LIST (broadcast)
    Note over R,D: Triggered by registration,<br/>deregistration, or periodic timer

    S->>R: UNREGISTER
    R->>S: UNREGISTER_ACK
    R->>D: SERVICE_LIST (updated)
```

### 4.3 SERVICE_LIST Format
```
Frame 0: msgId = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: Service entries (repeated service_count times)
  - service_type (uint16_t)
  - service_name (string)
  - provider_count (uint32_t)
  - provider entries (repeated provider_count times):
      service_role (uint16_t), endpoint (string),
      routing_id, weight (uint32_t)
```

## 5. SPOT Internal Implementation

### 5.1 Structure
- `spot_node_t` -- Network control (owns PUB/SUB sockets, mesh management, worker thread)
- `spot_pub_t` -- Publish handle (delegates to spot_node_t's publish, tag-based validity check)
- `spot_sub_t` -- Subscribe/receive handle (internal queue, pattern matching, condition variable-based blocking recv)

### 5.2 Concurrency Model
- Publishing: Performed directly on the caller's thread, serialized by `_publish_sync` mutex (thread-safe)
- Receiving: Worker thread receives from SUB socket → distributes to spot_sub_t internal queues
- Lock ordering: `_sync` → `_publish_sync` (deadlock prevention)
- Direct publishing without async queue (no message buffering on the publish path)

### 5.3 Subscription Aggregation
- Refcount-based SUB filter management
- Duplicate subscriptions to the same topic increment the refcount
- Per-spot_sub_t subscription set management (separate for exact topics and patterns)

### 5.4 Delivery Policy
- Local publish (spot_pub) → local spot_sub distribution + PUB output (remote propagation)
- Remote receive (SUB) → local spot_sub distribution only (no re-publishing, loop prevention)

### 5.4.1 SpotNode HWM Boundaries
- Unified `Spot` handle HWM and SpotNode internal HWM are different layers.
- `Spot` handle HWM controls the public facade pub/sub sockets.
- `SpotNode` HWM is the internal data-plane budget and is applied by direction:
  - `SNDHWM` → `fanout`, `mesh_pub`
  - `RCVHWM` → `ingress`, `mesh_xsub`
- The default SpotNode internal data-plane HWM is `1000`.
- `peer_ctrl` is a control-plane socket and is not grouped into the SpotNode
  data-plane HWM family.

### 5.5 Raw Socket Policy
- `spot_pub_t`: Does not expose raw PUB socket (prevents thread-safety bypass)
- `spot_sub_t`: Does not expose raw SUB socket; consumption is via callback/recv API only

### 5.6 Discovery Type Segmentation
- Separates spot_node/socket_family via service_type field
  - `service_type_spot_node` (2), `service_type_socket` (3)
- Socket family services additionally carry a `service_role` field
  (ROUTER=3, DEALER=4, PUB=5, SUB=6) for role-based peer matching
- Role matching is enforced by `service_roles_match()` -- PUB pairs with SUB,
  ROUTER/DEALER pair with each other

## 6. SPOT Internal Architecture

For detailed SPOT/SpotNode internal architecture including component
diagrams, all 11 internal sockets with types/endpoints/HWM, topic and
routed message flow sequences, control plane, and data plane polling,
see the dedicated document: **[SPOT Internals](spot-internals.md)**.

### 6.1 Component Diagram

```mermaid
flowchart TB
    subgraph PublicAPI["Public C API"]
        spot_handle["spot_handle_t<br/>(unified facade)"]
        spot_node_api["spot_node API"]
    end

    subgraph AccessLayer["Access Layer"]
        subject_access["spot_subject_access"]
        node_access["spot_node_access"]
    end

    subgraph ControlPlane["Control Plane"]
        spot_node["spot_node_t<br/>peer state, lifecycle,<br/>handle management"]
        control_task["control_task (10ms)<br/>subscription replay,<br/>ready refresh"]
    end

    subgraph Runtime["Runtime"]
        spot_runtime["spot_runtime_t<br/>socket attachments,<br/>batch/HWM config"]
    end

    subgraph DataPlane["Data Plane (separate thread)"]
        dp_loop["spot_data_plane_loop<br/>main polling loop"]
        dp_forwarding["forwarding<br/>batching, encoding"]
        dp_protocol["protocol<br/>control msgs, bootstrap"]
    end

    subgraph InprocSockets["Inproc Socket Network"]
        pub_ingress["pub_ingress (SUB)"]
        sub_fanout["sub_fanout (XPUB)"]
        mesh_pub["mesh_pub (PUB)"]
        mesh_xsub["mesh_xsub (XSUB)"]
        route_ingress["route_ingress (ROUTER)"]
        node_router["node_router (ROUTER)"]
        ctrl_pair["ctrl (PAIR)"]
    end

    spot_handle --> subject_access
    spot_node_api --> node_access
    subject_access --> spot_node
    node_access --> spot_node
    spot_node --> control_task
    spot_node --> spot_runtime
    spot_runtime --> dp_loop
    dp_loop --> dp_forwarding
    dp_loop --> dp_protocol
    dp_loop --> pub_ingress
    dp_loop --> sub_fanout
    dp_loop --> mesh_pub
    dp_loop --> mesh_xsub
    dp_loop --> route_ingress
    dp_loop --> node_router
    dp_loop --> ctrl_pair
```

### 6.2 Inproc Socket Topology

All inproc paths: `inproc://zlink.spot.{node_id}.{purpose}`

| Endpoint | Socket Type | Direction | Purpose |
|----------|------------|-----------|---------|
| `.pub-in` | SUB | local pubs → data plane | Topic publish ingress |
| `.sub-out` | XPUB | data plane → local subs | Topic subscribe fanout |
| `.route-in` | ROUTER | local senders → data plane | Routed message ingress |
| `.node-router` | ROUTER | data plane → local receivers | Routed message delivery |
| `.ctrl` | PAIR | control plane ↔ data plane | Internal commands |

### 6.3 Topic Message Internal Flow

```mermaid
sequenceDiagram
    participant Pub as spot_pub_t
    participant Ingress as pub_ingress (SUB)
    participant DP as Data Plane Loop
    participant MeshPub as mesh_pub (PUB)
    participant Fanout as sub_fanout (XPUB)
    participant Sub as spot_sub_t

    Pub->>Ingress: publish(topic, parts) via inproc
    Ingress->>DP: poll readable → receive message
    DP->>Fanout: local fanout (immediate)
    Fanout->>Sub: deliver to matching subscribers
    DP->>MeshPub: send immediately
    Note over MeshPub: → remote peers via tcp mesh
```

### 6.4 Routed Message Internal Flow

```mermaid
sequenceDiagram
    participant Sender as spot_send_spot()
    participant RouteIn as route_ingress (ROUTER)
    participant DP as Data Plane Loop
    participant NodeRouter as node_router (ROUTER)
    participant Receiver as spot_recv / spot_handler

    Sender->>RouteIn: send with SPOT routed envelope (8 parts)
    RouteIn->>DP: poll readable → receive routed message
    DP->>DP: parse SPOT envelope → resolve destination
    alt Destination is local
        DP->>NodeRouter: forward via inproc
        NodeRouter->>Receiver: deliver to spot_handler or recv queue
    else Destination is remote
        DP->>DP: forward via peer ROUTER-ROUTER transport
        Note over DP: remote data plane delivers locally
    end
```

### 6.5 SPOT Request-Reply Dispatch

```mermaid
sequenceDiagram
    participant App as Application
    participant API as spot_request_spot()
    participant State as spot_request_reply_state
    participant Sched as Timeout Scheduler
    participant DP as Data Plane
    participant Remote as Remote Spot

    App->>API: request(dest_node, dest_spot, payload, timeout)
    API->>API: build SPOT envelope (8) + RR envelope (4)
    API->>State: register pending[key]
    API->>Sched: schedule(deadline, on_timeout)
    API->>DP: send [12 control parts] + [payload]
    DP->>Remote: forward to destination

    Remote->>DP: reply [12 control parts] + [reply payload]
    DP->>API: internal dispatch
    API->>State: lookup pending[key]
    API->>Sched: cancel timeout
    API->>State: remove pending[key]
    API->>App: reply_handler(0, reply_parts)
```

### 6.6 SPOT Routed Request-Reply Combination

SPOT request-reply has separate state from the topic fanout path.
The implementation decodes in three stages:

1. Decode SPOT routed envelope (8 control parts)
2. Decode request-reply envelope (4 control parts) from remaining payload
3. If request → dispatch to local handler; if reply → complete pending map

Structure:

- SPOT routed envelope: source/destination node, spot, router addresses
- request-reply envelope: `message_type`, `request_seq`
- payload: application body

### 6.7 Pending Structures

Socket request-reply and SPOT request-reply use different pending keys:

```cpp
struct pending_key_t {          // Socket level
    std::string peer_rid;
    uint64_t request_seq;
};

struct pending_spot_key_t {     // SPOT level
    uint8_t source_class;
    std::string source_rid;
    std::string source_spot_rid;
    uint64_t request_seq;
};
```

| API | Pending Key | Reason |
|-----|------------|--------|
| DEALER | `request_seq` only | Single peer, seq is unique |
| ROUTER | `source_node_rid + request_seq` | Multiple peers may reuse seq. For SPOT-originated routed traffic, `source_spot_rid` is also carried through the dispatch path so the unified router handler can distinguish plain vs. SPOT-routed callers |
| spot → spot | `source_class + source_address + request_seq` | Multiple sources |
| router → spot | `request_seq` | Local router state |

### 6.8 Timeout and Completion

Each request registers a pending entry with a timeout task:

- Per-call timeout is used if provided
- Otherwise socket/spot default timeout applies
- If both are 0, implementation default `5000ms` applies

Completion rules:

- Timeout fires first → remove pending, callback with `ETIMEDOUT`
- Reply arrives first → remove pending, cancel timeout (no-op if already fired)
- Extra replies to a completed key are silently dropped
- `error_reply` reads 4-byte errno from first payload part → failure completion

## 7. Request-Reply Dispatch Architecture

### 7.1 Socket-Level Dispatch Component Diagram

```mermaid
flowchart TB
    subgraph PublicAPI["Public API"]
        dealer_req["zlink_dealer_request()"]
        router_req["zlink_router_request()"]
        router_reply["zlink_router_reply()"]
        router_recv["zlink_router_recv()"]
    end

    subgraph State["Per-Socket State"]
        rr_state["socket_request_reply_state_t<br/>pending_sequences,<br/>pending_requests map"]
    end

    subgraph Dispatch["Internal Dispatch"]
        msg_dispatch["socket_request_reply_dispatch()<br/>installed as socket msg handler"]
        envelope_parse["parse_envelope()<br/>extract protocol_id, message_type,<br/>request_seq"]
    end

    subgraph Queue["Internal Pair Queue"]
        tx["tx (PAIR sender)"]
        rx["rx (PAIR receiver)"]
    end

    subgraph Scheduler["Timeout Scheduler"]
        timeout_thread["global timeout thread"]
        timeout_schedule["deadline multimap"]
    end

    dealer_req --> rr_state
    router_req --> rr_state
    rr_state --> msg_dispatch
    msg_dispatch --> envelope_parse

    envelope_parse -->|request| tx
    tx -.->|inproc PAIR| rx
    router_recv --> rx

    envelope_parse -->|reply| rr_state
    rr_state -->|match pending| timeout_schedule
    rr_state -->|invoke| dealer_req

    router_req --> timeout_schedule
    dealer_req --> timeout_schedule
```

### 7.2 Dispatch Sequence (Reply Completion)

```mermaid
sequenceDiagram
    participant Net as Network
    participant Socket as ROUTER/DEALER Socket
    participant Dispatch as request_reply_dispatch

    Net->>Socket: incoming message
    Socket->>Dispatch: msg_handler callback
    Dispatch->>Dispatch: parse_envelope()
    alt message_type = reply
        Dispatch->>Dispatch: lookup pending[source_node_rid + seq]
        Dispatch->>Dispatch: cancel timeout task
        Dispatch->>Dispatch: invoke reply_handler(errno, parts, userdata)
    else message_type = error_reply
        Dispatch->>Dispatch: decode errno from first payload part
        Dispatch->>Dispatch: invoke reply_handler(errno, NULL, userdata)
    end
```

### 7.3 Dispatch Sequence (Router Recv Path)

```mermaid
sequenceDiagram
    participant Net as Network
    participant Socket as ROUTER Socket
    participant Dispatch as request_reply_dispatch
    participant Queue as Internal Pair Queue
    participant App as zlink_router_recv()

    Net->>Socket: incoming routed message
    Socket->>Dispatch: msg_handler callback
    Dispatch->>Dispatch: parse_envelope() → request (or plain routed)
    Dispatch->>Queue: enqueue [source_node_rid, source_spot_rid, request_seq, payload]
    Note over Queue: via internal PAIR socket (inproc)

    App->>Queue: recv from internal PAIR
    Queue->>App: [source_node_rid, source_spot_rid, request_seq, payload]
    App->>App: return to caller
```

## 8. Timer and Scheduler Architecture

### 8.1 Component Diagram

```mermaid
flowchart TB
    subgraph PublicAPI["Public Timer API"]
        timer_new["zlink_timer_new()"]
        spot_timer["zlink_spot_timer_new(spot)"]
        timer_start["zlink_timer_start()"]
        timer_recv["zlink_timer_recv()"]
        timer_handler["zlink_timer_handler()"]
    end

    subgraph TimerHandle["timer_handle_t"]
        state["interval_ns, repeat_count,<br/>running, stop_requested"]
        fired["fired_counts deque"]
        signaler["signaler_t (eventfd)"]
        handler_fn["handler callback"]
    end

    subgraph GlobalSched["Global Shared Scheduler"]
        g_thread["worker thread"]
        g_schedule["deadline multimap"]
        g_cv["condition variable"]
    end

    subgraph SpotSched["SpotNode-Local Schedulers"]
        s_thread["worker thread (per node)"]
        s_schedule["deadline multimap"]
    end

    subgraph Poller["Poller Integration"]
        poller["zlink_poller_wait()"]
        fd_reg["FD registration"]
    end

    timer_new --> GlobalSched
    spot_timer --> SpotSched
    timer_start --> TimerHandle
    TimerHandle --> GlobalSched
    TimerHandle --> SpotSched

    g_thread -->|fire| handler_fn
    g_thread -->|fire, no handler| fired
    fired --> signaler
    signaler --> fd_reg
    fd_reg --> poller

    timer_recv --> fired
    timer_handler --> handler_fn
```

### 8.2 Timer Fire Sequence

```mermaid
sequenceDiagram
    participant Sched as Scheduler Thread
    participant Timer as timer_handle_t
    participant App as Application

    Sched->>Sched: cv.wait_for(next deadline)
    Sched->>Timer: scheduler_fire_timer()

    alt Callback mode (handler set)
        Timer->>App: handler(timer, fire_count, userdata)
    else Recv/Poller mode (no handler)
        Timer->>Timer: push fire_count to deque
        Timer->>Timer: signaler.send() (eventfd)
        Note over Timer: wakes poller or unblocks recv
    end

    Sched->>Sched: check repeat_count
    alt repeat_count > 0 and not exhausted
        Sched->>Sched: reschedule at deadline + interval
    else repeat_count exhausted
        Sched->>Timer: mark stopped
    end
```

### 8.3 Request Timeout Scheduler

The request timeout scheduler is **separate** from the timer scheduler.
It is dedicated to request-reply timeout management.

```mermaid
flowchart LR
    subgraph TimeoutSched["Global Timeout Scheduler"]
        thread["single worker thread"]
        schedule["deadline multimap<br/>(deadline → task)"]
        cv["condition variable"]
    end

    subgraph Task["timeout_task_t"]
        deadline["deadline_ns"]
        handler["on_timeout callback"]
        state["registered, canceled,<br/>firing, completed"]
    end

    start_request -->|schedule| TimeoutSched
    TimeoutSched -->|fires| Task
    Task -->|callback| remove_pending
    cancel_timeout -->|cancel| Task
```

- Single global thread for all request timeouts
- Efficient for many short-lived timeouts
- Task has cancel support with fire/cancel race resolution

## 9. Internal Pair Queue Mechanism

The internal pair queue bridges the gap between internal dispatch
(on the I/O thread) and user recv calls (on the application thread).

```mermaid
flowchart LR
    subgraph IOThread["I/O Thread"]
        dispatch["request_reply_dispatch()"]
    end

    subgraph PairQueue["Internal Pair Queue"]
        tx["tx (PAIR)"]
        inproc["inproc://zlink.{type}.reqrep.recv-{ptr}"]
        rx["rx (PAIR)"]
    end

    subgraph AppThread["Application Thread"]
        recv["zlink_router_recv()"]
    end

    dispatch -->|send frames| tx
    tx ---|inproc PAIR| rx
    rx -->|recv frames| recv
```

Structure:

```cpp
struct internal_pair_queue_t {
    socket_base_t *rx;     // receive side (application thread)
    socket_base_t *tx;     // send side (dispatch thread)
    std::string endpoint;  // unique inproc endpoint
};
```

Queue creation (`ensure()`):
1. Generate unique inproc endpoint
2. Create two PAIR sockets: rx (bind), tx (connect)
3. Bidirectional handshake (0x11 → 0x22 → back)
4. Set linger = 0 for clean shutdown

Frame encoding for ROUTER recv queue (unified routed surface — the
queue carries both plain ROUTER traffic and SPOT-originated routed
traffic through the same framing):
- Frame 1: `source_node_rid` bytes
- Frame 2: `source_spot_rid` bytes (zero length for plain ROUTER traffic)
- Frame 3: `request_seq` (8 bytes Big Endian; `0` for fire-and-forget)
- Frame 4+: Payload parts

## 10. Admission state propagation

Both raw ROUTER and SpotNode drive their own admission state through
`zlink_set_admission_state()`. Internally each subject advertises the
change to its connected peers as a **best-effort runtime signal**, and
each peer updates its admission cache so outbound candidate selection
reflects the new state.

Baseline behavior:

- A state change is applied to the local cache immediately. Other local
  outbound paths on the same node (e.g. local spot or router send) see the
  new state right away.
- Peer-side propagation flows through the SpotNode peer control path
  (`peer_ctrl_pub` / `peer_ctrl_sub`) and through the dedicated raw socket
  admission signal path. The signal is a best-effort runtime control
  message; no strong synchronous model is provided.
- After reconnect the admission state resyncs. When a new session becomes
  ready the subject re-advertises its current admission state once so a
  stale cache does not cause incorrect candidate selection.
- When a peer's cache shows `DRAINING`, the peer drops that target from
  outbound candidate selection. If every candidate is `DRAINING`, the
  submit path normalizes to `ZLINK_SUBMIT_NOT_ADMITTED`. Under races where
  connection state changes are observed before the admission cache update,
  the same refusal may surface first as `ZLINK_SUBMIT_NOT_CONNECTED` or
  `ZLINK_SUBMIT_NOT_FOUND`.
- Raw socket changes are exposed to the application via the socket monitor
  event `ZLINK_EVENT_PEER_ADMISSION_CHANGED`. SpotNode changes use the
  service monitor event
  `ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED`. The implementation
  carries both the peer identifier (`routing_id`) and the new admission
  state inside the same event payload.

## 11. Pairwise initiator rule (Discovery auto-connect)

When two ROUTERs in the same service discover each other via Discovery,
the library decides internally that exactly one side dials. This is an
internal Discovery auto-connect rule, not a user-facing knob.

Comparison procedure:

1. Verify that local and remote share the same `service_name` and that
   both sides are in ROUTER role.
2. Build a stable comparison key for each side. The primary key is the
   `routing_id`; if `routing_id` is equal, the advertised endpoint string
   is used as the tie-break.
3. If the local key compares less than the remote key, the local side is
   chosen as initiator. Otherwise the local side does not generate the
   connect.
4. Both peers compute the same total order from the same inputs, so each
   pair has exactly one initiator.

Interaction with provider-snapshot refresh:

- Discovery sees a new provider set on every SERVICE_LIST update. Because
  the comparison is deterministic for any pair, snapshot refresh does not
  flap the initiator direction.
- Environments where `routing_id` changes after restart may see the
  initiator direction flip on the next run. This is not an error: the
  contract is "exactly one side dials per pair at any given time," not
  "the same side always dials."

Relationship with manual connect and handover:

- The rule applies only to Discovery-managed auto-connect. Manual
  `zlink_connect()` calls made through the raw API are not mediated by
  the library.
- Distinct peers that happen to share the same `routing_id` are not
  resolved by this rule. Such collisions are handled by the existing
  ROUTER handover policy.
- Pairwise initiator and handover are two separate layers: the initiator
  rule prevents duplicate dials up-front, while handover cleans up
  duplicates that still appear after the fact.
