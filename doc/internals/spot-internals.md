[English](spot-internals.md) | [한국어](spot-internals.ko.md)

# SPOT / SpotNode Internal Architecture

## 1. Component Overview

```mermaid
flowchart TB
    subgraph UserLayer["User Layer"]
        app["Application"]
        spot_handle["spot_handle_t<br/>(unified facade)"]
    end

    subgraph AccessLayer["Service Access Layer"]
        subject_access["spot_subject_access<br/>publish, subscribe,<br/>recv, handler"]
        node_access["spot_node_access<br/>bind, connect_peer,<br/>attach_discovery"]
    end

    subgraph NodeLayer["SpotNode (spot_node_t)"]
        node_core["spot_node_t<br/>lifecycle owner"]
        peer_state["peer_state<br/>manual/discovery/active endpoints"]
        handles["handle management<br/>pub/sub creation"]
        control_task["control_task (10ms)<br/>subscription replay,<br/>ready refresh"]
    end

    subgraph RuntimeLayer["Runtime (spot_runtime_t)"]
        runtime["spot_runtime_t<br/>socket container,<br/>batch config, HWM config"]
        attachments["attachment map<br/>(pub/sub sockets)"]
    end

    subgraph DataPlaneLayer["Data Plane (separate thread)"]
        dp_loop["spot_data_plane_loop_t<br/>main polling loop<br/>(7 sockets polled)"]
        dp_fwd["forwarding<br/>topic batching,<br/>encoding/decoding"]
        dp_proto["protocol<br/>control msgs,<br/>bootstrap descriptors"]
    end

    app --> spot_handle
    spot_handle --> subject_access
    spot_handle --> node_access
    subject_access --> node_core
    node_access --> node_core
    node_core --> peer_state
    node_core --> handles
    node_core --> control_task
    node_core --> runtime
    runtime --> attachments
    runtime --> dp_loop
    dp_loop --> dp_fwd
    dp_loop --> dp_proto
```

## 2. Internal Socket Topology

SpotNode creates 10 internal sockets connected via inproc endpoints,
plus a monitor socket for connection state tracking (11 total).

### 2.1 Socket Inventory

```mermaid
flowchart LR
    subgraph PubSide["Publisher Side"]
        spot_pub["spot_pub_t<br/>(PUB socket)"]
    end

    subgraph Internal["Data Plane Sockets"]
        ingress["ingress<br/>SUB socket<br/>BIND .pub-in"]
        fanout["fanout<br/>PUB socket<br/>BIND .sub-out"]
        mesh_pub["mesh_pub<br/>PUB socket"]
        mesh_xsub["mesh_xsub<br/>XSUB socket"]
        route_in["route_ingress<br/>ROUTER socket<br/>BIND .route-in"]
        node_router["node_router<br/>ROUTER socket<br/>BIND .node-router"]
        ctrl["ctrl<br/>PAIR socket"]
        peer_ctrl_pub["peer_ctrl_pub<br/>PUB socket"]
        peer_ctrl_sub["peer_ctrl_sub<br/>SUB socket"]
    end

    subgraph SubSide["Subscriber Side"]
        spot_sub["spot_sub_t<br/>(SUB socket)"]
    end

    subgraph Remote["Remote Peers"]
        remote_node["Other SpotNodes"]
    end

    spot_pub -->|connect .pub-in| ingress
    ingress -->|"topic forward"| fanout
    ingress -->|"mesh forward"| mesh_pub
    fanout -->|connect .sub-out| spot_sub
    mesh_pub -->|"tcp/tls"| remote_node
    remote_node -->|"tcp/tls"| mesh_xsub
    mesh_xsub -->|"topic forward"| fanout
    peer_ctrl_pub -->|control| remote_node
    remote_node -->|control| peer_ctrl_sub
```

### 2.2 Socket Details

| Socket | Type | Endpoint | Bind/Connect | HWM | Role |
|--------|------|----------|-------------|-----|------|
| `ingress` | SUB | `.pub-in` | BIND | `node_sub_rcvhwm` | Receives all local publishes |
| `fanout` | PUB | `.sub-out` | BIND | `node_pub_sndhwm` | Distributes to local subscribers |
| `mesh_pub` | PUB | (bound endpoint) | BIND | `node_pub_sndhwm` | Sends topics to remote peers |
| `mesh_xsub` | XSUB | — | CONNECT to peers | `node_sub_rcvhwm` | Receives topics from remote peers |
| `route_ingress` | ROUTER | `.route-in` | BIND | `routed_recv_hwm` | Receives routed messages from apps |
| `node_router` | ROUTER | `.node-router` | BIND | `routed_send/recv_hwm` | Delivers routed messages to apps |
| `ctrl` | PAIR | `.ctrl` | CONNECT | — | Control plane ↔ data plane commands |
| `peer_ctrl_pub` | PUB | (derived from bound) | BIND | 1024 | Sends control msgs to peers |
| `peer_ctrl_sub` | SUB | — | CONNECT to peers | 1024 | Receives control msgs from peers |
| `mesh_xsub_monitor` | Monitor | — | — | — | Tracks CONNECTION_READY/DISCONNECTED |

All inproc endpoints follow the pattern: `inproc://zlink.spot.{node_id}.{suffix}`

### 2.3 Common Socket Settings

All data plane sockets share:
- `LINGER = 0`
- `SNDTIMEO = -1` (blocking)
- `ingress`: `SUBSCRIBE = ""` (receive all topics)
- `fanout`: `XPUB_NODROP = 1` (do not drop for slow subscribers; applied to internal PUB socket)
- `peer_ctrl_sub`: `SUBSCRIBE = "__zlink.spot.ctrl."` (control prefix only)

## 3. Pub/Sub Attachment

User-facing `spot_pub_t` and `spot_sub_t` connect to the data plane
via separate attachment sockets.

```mermaid
sequenceDiagram
    participant App as Application
    participant Pub as spot_pub_t (PUB)
    participant Ingress as ingress (SUB)
    participant DP as Data Plane
    participant Fanout as fanout (XPUB)
    participant Sub as spot_sub_t (SUB)

    Note over Pub,Ingress: PUB connects to .pub-in (SUB binds)
    Note over Fanout,Sub: XPUB binds .sub-out (SUB connects)

    App->>Pub: zlink_publish(spot, topic, parts)
    Pub->>Ingress: send via inproc
    Ingress->>DP: poller wakes → recv
    DP->>Fanout: local fanout (immediate)
    Fanout->>Sub: deliver to matching subscriber
    Sub->>App: zlink_subscribe() or subscribe_handler callback
```

### Attachment Creation

```mermaid
sequenceDiagram
    participant Node as spot_node_t
    participant RT as spot_runtime_t
    participant PUB as New PUB Socket
    participant SUB as New SUB Socket

    Node->>RT: create_attachment(pub, pub_ingress_endpoint)
    RT->>PUB: create PUB socket
    RT->>PUB: connect("inproc://zlink.spot.{id}.pub-in")
    RT->>RT: store in attachment_map

    Node->>RT: create_attachment(sub, sub_fanout_endpoint)
    RT->>SUB: create SUB socket
    RT->>SUB: connect("inproc://zlink.spot.{id}.sub-out")
    RT->>RT: store in attachment_map
```

## 4. Topic Message Flow (Detailed)

### 4.1 Local Publish → Local Subscribe

```mermaid
sequenceDiagram
    participant Pub as spot_pub_t
    participant Ingress as ingress (SUB)
    participant DP as data_plane_loop
    participant Fanout as fanout (XPUB)
    participant Sub as spot_sub_t

    Pub->>Ingress: [topic] + [payload parts]
    Note over DP: poller → ingress readable
    DP->>DP: recv_and_forward_ingress()
    DP->>DP: check has_local_filtered_subs
    DP->>Fanout: send [topic] + [payload]
    Note over Fanout: XPUB matches subscriptions
    Fanout->>Sub: deliver to matching subs
```

### 4.2 Local Publish → Remote Peers

```mermaid
sequenceDiagram
    participant Pub as spot_pub_t
    participant Ingress as ingress (SUB)
    participant DP as data_plane_loop
    participant MeshPub as mesh_pub (PUB)
    participant Remote as Remote SpotNode

    Pub->>Ingress: [topic] + [payload parts]
    Note over DP: poller → ingress readable
    DP->>DP: recv_and_forward_ingress()

    DP->>MeshPub: send [topic] + [payload]

    MeshPub->>Remote: via tcp/tls mesh
```

### 4.3 Remote Receive → Local Subscribe

```mermaid
sequenceDiagram
    participant Remote as Remote SpotNode
    participant MeshXSub as mesh_xsub (XSUB)
    participant DP as data_plane_loop
    participant Fanout as fanout (XPUB)
    participant Sub as spot_sub_t

    Remote->>MeshXSub: topic message via tcp mesh
    Note over DP: poller → mesh_xsub readable
    DP->>DP: recv_and_dispatch_mesh_xsub()
    DP->>Fanout: forward to local fanout
    Fanout->>Sub: deliver to matching subs
    Note over DP: NEVER re-publish to mesh_pub<br/>(loop prevention)
```

## 5. Routed Message Flow (Detailed)

### 5.1 Local spot → Local spot (same node)

```mermaid
sequenceDiagram
    participant Sender as spot_send_spot()
    participant RouteIn as route_ingress (ROUTER)
    participant DP as data_plane_loop
    participant NodeRouter as node_router (ROUTER)
    participant Receiver as spot_handler / spot_recv

    Sender->>RouteIn: [SPOT envelope 8 parts] + [payload]
    Note over RouteIn: ROUTER prepends sender routing_id
    Note over DP: poller → route_ingress readable
    DP->>DP: parse SPOT envelope
    DP->>DP: destination = local spot
    DP->>NodeRouter: forward [SPOT envelope] + [payload]
    Note over NodeRouter: ROUTER routes to target spot_rid
    NodeRouter->>Receiver: deliver via handler or recv queue
```

### 5.2 Local spot → Remote spot (cross-node)

```mermaid
sequenceDiagram
    participant Sender as spot_send_spot()
    participant RouteIn as route_ingress (ROUTER)
    participant DP1 as Data Plane (Node 1)
    participant Net as ROUTER-ROUTER Transport
    participant DP2 as Data Plane (Node 2)
    participant NodeRouter2 as node_router (Node 2)
    participant Receiver as spot_handler (Node 2)

    Sender->>RouteIn: [SPOT envelope] + [payload]
    DP1->>DP1: parse envelope → dest_node = Node 2
    DP1->>Net: forward via peer ROUTER connection
    Net->>DP2: deliver to Node 2 data plane
    DP2->>DP2: parse envelope → dest is local spot
    DP2->>NodeRouter2: forward to local node_router
    NodeRouter2->>Receiver: deliver to target spot
```

### 5.3 spot → router / router → spot

```mermaid
sequenceDiagram
    participant Spot as spot_send_router()
    participant RouteIn as route_ingress (ROUTER)
    participant DP as Data Plane
    participant Peer as ROUTER peer (transport)

    Spot->>RouteIn: [SPOT envelope: dest_class=router] + [payload]
    DP->>DP: parse → destination is ROUTER peer
    DP->>Peer: forward via transport routing_id
```

## 6. Control Plane

### 6.1 Control Task Cycle (10ms)

```mermaid
flowchart TD
    start["control_task tick (10ms)"] --> replay["subscription replay<br/>(exponential holdoff)"]
    replay --> ready["refresh subscription<br/>ready state"]
    ready --> pub_ready["refresh publisher<br/>delivery ready"]
    pub_ready --> peer_sync["peer state<br/>synchronization"]
    peer_sync --> bootstrap["publish bootstrap<br/>descriptor (if needed)"]
    bootstrap --> done["done"]
```

### 6.2 Peer Control Messages

Peer control endpoints are derived from the bound data endpoint:

| Transport | Data Endpoint | Control Endpoint |
|-----------|--------------|-----------------|
| tcp | `tcp://host:9000` | `tcp://host:10000` (port+1000) |
| tls | `tls://host:9000` | `tls://host:10000` |
| ipc | `ipc:///path` | `ipc:///path.zlink-spot-ctrl.{id}` |
| inproc | `inproc://name` | `inproc://zlink.spot.peer-ctrl.{id}` |

Control message prefixes:
- `__zlink.spot.ctrl.snapshot` — status snapshots
- `__zlink.spot.ctrl.ready_ack` — subscription readiness acks
- `__zlink.spot.bootstrap.ctrl_descriptor` — bootstrap info

## 7. Data Plane Polling Loop

The data plane runs in a separate thread. It polls 7 sockets:

```mermaid
flowchart LR
    subgraph Poller["spot_data_plane_loop (7 sockets)"]
        ctrl_poll["ctrl (PAIR)"]
        ingress_poll["ingress (SUB)"]
        mesh_poll["mesh_xsub (XSUB)"]
        peer_poll["peer_ctrl_sub (SUB)"]
        route_poll["route_ingress (ROUTER)"]
        node_poll["node_router (ROUTER)"]
        mon_poll["mesh_xsub_monitor"]
    end

    ctrl_poll -->|"commands"| process_ctrl
    ingress_poll -->|"local topics"| forward_to_fanout_and_mesh
    mesh_poll -->|"remote topics"| forward_to_fanout
    peer_poll -->|"control msgs"| process_ctrl_messages
    route_poll -->|"routed ingress"| process_route_ingress
    node_poll -->|"routed delivery"| process_node_router
    mon_poll -->|"connection events"| update_peer_state
```

## 8. Unified Handle (spot_handle_t)

```cpp
struct spot_handle_t {
    uint32_t tag;                                // Validation tag
    spot_node_t *node;                           // Parent SpotNode
    spot_pub_t *pub;                             // Publisher (inproc PUB → ingress)
    spot_sub_t *sub;                             // Subscriber (inproc SUB ← fanout)
    zlink_subscribe_handler_fn handler;          // Topic callback
    void *handler_userdata;
    spot_node_t::pub_defaults_t pending_pub_defaults;
    spot_node_t::sub_defaults_t pending_sub_defaults;
    service_mode_state_t mode_state;             // recv/callback mode tracking
    std::shared_ptr<void> request_reply_state;   // Per-handle RR state
};
```

The unified handle borrows the SpotNode. Multiple handles can share one node.
Each handle has its own pub/sub pair, mode state, and request-reply state.

## 9. HWM Boundaries

```text
+------------------------------------------------------------------+
|  Spot Handle HWM                                                  |
|  (public facade pub/sub sockets)                                  |
|  ┌──────────────────────────────────────────────────────────────┐ |
|  │  SpotNode Data-Plane HWM                                     │ |
|  │  ┌─────────────────────────┬────────────────────────────┐    │ |
|  │  │  SNDHWM applied to:     │  RCVHWM applied to:        │    │ |
|  │  │  - fanout (PUB)         │  - ingress (SUB)            │    │ |
|  │  │  - mesh_pub (PUB)       │  - mesh_xsub (XSUB)        │    │ |
|  │  │  - node_router (SND)    │  - route_ingress (ROUTER)   │    │ |
|  │  │                         │  - node_router (RCV)        │    │ |
|  │  └─────────────────────────┴────────────────────────────┘    │ |
|  │                                                               │ |
|  │  peer_ctrl is CONTROL PLANE → separate HWM (1024)            │ |
|  └──────────────────────────────────────────────────────────────┘ |
+------------------------------------------------------------------+
```

Default internal data-plane HWM: `1000`

Topic and routed HWM can be configured independently via
`zlink_set_spot_node_option()`:
- `ZLINK_SPOT_NODE_OPT_TOPIC_SNDHWM` / `ZLINK_SPOT_NODE_OPT_TOPIC_RCVHWM`
- `ZLINK_SPOT_NODE_OPT_ROUTED_SNDHWM` / `ZLINK_SPOT_NODE_OPT_ROUTED_RCVHWM`

## 10. Dispatch Event Threading Model

The public-facing `zlink_spot_dispatch_event_handler()` surface is a
notification-only callback that fans in **three independent internal
event producers** onto one handler. This section documents which
internal thread fires each event, how the registration is enforced, and
the thread-safety boundaries the callback has to respect.

### 10.1 Event producers and their threads

```mermaid
flowchart LR
    subgraph DataPlane["SpotNode data-plane thread"]
        ingress["sub plane<br/>spot_sub readable"]
        routed["routed dispatch<br/>(node_router → queue)"]
    end

    subgraph Scheduler["SpotNode-local timer scheduler thread"]
        tick["scheduler_fire_timer()"]
    end

    subgraph UserHandler["zlink_spot_dispatch_event_handler (one per Spot)"]
        handler["zlink_spot_dispatch_event_handler_fn"]
    end

    ingress -->|"SUBSCRIBE_READABLE"| handler
    routed  -->|"ROUTED_READABLE"| handler
    tick    -->|"TIMER_READABLE"| handler
```

| Event | Source producer | Thread that fires the callback |
|-------|----------------|-------------------------------|
| `ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE` | `spot_sub_handler_adapter` in `service_handler_spot_api.cpp` — installed as the direct handler of the `spot_sub_t` | SpotNode data-plane polling thread (see §7 *Data Plane Polling Loop*) |
| `ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE` | `queue_spot_message()` in `service_spot_request_reply_api.cpp` — invoked after enqueuing a routed delivery into the internal PAIR queue | SpotNode data-plane polling thread (the same thread that parsed the routed envelope from `node_router` / mesh ingress) |
| `ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE` | `scheduler_fire_timer()` in `timer_scheduler_backend.cpp` — invoked *after* pushing the fire-count into the timer's deque and raising its signaler, and *only* when no direct timer handler is attached | SpotNode-local timer scheduler thread (separate from the data-plane thread) |

All three producers reach the callback through one shared entry point,
`zlink_spot_notify_dispatch_event()` → `maybe_dispatch_spot_event()`, which
reads the handler pointer under the per-Spot mutex and then invokes it
without holding any internal lock:

```cpp
void maybe_dispatch_spot_event (spot_request_reply_state_t *state_,
                                zlink_spot_dispatch_event_t event_)
{
    zlink_spot_dispatch_event_handler_fn handler = NULL;
    void *userdata = NULL;
    void *owner = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->dispatch_event_handler;
        userdata = state_->dispatch_event_handler_userdata;
        owner = state_->owner;
    }
    if (handler)
        handler (owner, event_, userdata);
}
```

The snapshot-then-invoke pattern is deliberate. The handler runs while
no zlink-internal locks are held, so the application is free to call
back into the zlink API (e.g. `zlink_timer_recv()`, `zlink_spot_recv()`,
`zlink_subscribe()`) from inside the callback — but see §10.4 for why it
is still recommended to hand off to an application worker.

### 10.2 Registration and mutual exclusion

The per-Spot `spot_request_reply_state_t` owns two handler slots on the
routed/dispatch axis:

```cpp
struct spot_request_reply_state_t {
    // ...
    zlink_spot_handler_fn                  request_handler;            // direct routed
    void                                  *request_handler_userdata;
    zlink_spot_dispatch_event_handler_fn   dispatch_event_handler;     // unified notify
    void                                  *dispatch_event_handler_userdata;
};
```

`zlink_spot_handler()` and `zlink_spot_dispatch_event_handler()` both
check `state->request_handler || state->dispatch_event_handler` under
`state->mutex` before writing, and return `ZLINK_HANDLER_BUSY` if either
slot is non-NULL. This is the source of the "mutually exclusive" rule
documented in the user guide.

`zlink_subscribe_handler()` is on a different axis — it installs a
direct subscribe callback on the underlying `spot_sub_t` and is not
blocked by `dispatch_event_handler`. Mixing the two is technically
allowed but defeats the unified-worker benefit.

### 10.3 Per-event fire conditions

**`SUBSCRIBE_READABLE`** — fired from `spot_sub_handler_adapter` on the
data-plane thread whenever the `spot_sub_t` direct handler is invoked.
The adapter calls `zlink_spot_notify_dispatch_event()` *before* the
composite user subscribe handler runs; if no user subscribe handler is
installed, the subscribe messages are queued inside the `spot_sub_t`
recv buffer and the notifier is still the wake-up signal for
`zlink_subscribe()` pull consumers.

**`ROUTED_READABLE`** — fired from `queue_spot_message()` once the
routed payload (node rid, spot rid, request_seq, parts) has been
enqueued onto the per-Spot internal PAIR queue (`inproc://zlink.spot.
routed.recv.*`). The event is fired *after* the queue write succeeds,
so a worker observing the notification is guaranteed to see at least
one payload on the next `zlink_spot_recv()`. When `request_handler` is
installed instead, the routed direct callback is invoked in place of
the queue write and the notification is not fired.

**`TIMER_READABLE`** — fired from `scheduler_fire_timer()` only when the
timer has an owning Spot (created via `zlink_spot_timer_new(spot)`) and
**no direct timer handler** is attached. In that branch, the scheduler
first pushes the fire count into `timer->fired_counts`, then raises
`timer->signaler` (eventfd), then calls
`zlink_spot_notify_dispatch_event(owner_spot, TIMER_READABLE)`. If a
direct `zlink_timer_handler()` is attached to the same timer, the
scheduler runs that handler inline and the dispatch event is not fired
— this is the per-timer precedence rule noted in the user guide.

### 10.4 End-to-end flow with an application worker

```mermaid
sequenceDiagram
    participant DP as Data-plane thread
    participant Sched as Timer scheduler thread
    participant Notify as notify_dispatch_event
    participant UH as User event handler
    participant App as App worker thread
    participant Q as Internal queues<br/>(sub buffer / routed PAIR / timer deque)

    alt topic message arrives
        DP->>Q: push to sub buffer
        DP->>Notify: SUBSCRIBE_READABLE
    else routed message arrives
        DP->>Q: push to routed PAIR
        DP->>Notify: ROUTED_READABLE
    else spot-owned timer fires
        Sched->>Q: push fire_count + signal eventfd
        Sched->>Notify: TIMER_READABLE
    end
    Notify->>UH: handler(spot, event, userdata)
    UH->>App: wake worker (cv / eventfd / channel)
    App->>Q: zlink_subscribe / zlink_spot_recv / zlink_timer_recv
    Q-->>App: payload / fire_count
```

The producers never execute application-domain logic past
`notify_dispatch_event`. All message decoding, topic matching, and
timer rescheduling happen on the internal threads; the worker thread
only touches queues that are already drained or appended atomically
(via `internal_pair_queue_t` for routed, `spot_sub_t` recv buffer for
topic, and the timer's `fired_counts` deque guarded by `timer->mutex`).

### 10.5 Thread-safety invariants

| Invariant | Enforced by |
|---|---|
| Handler pointer read is race-free | Snapshot under `state->mutex` in `maybe_dispatch_spot_event` |
| Handler is invoked without internal locks held | Snapshot-then-release before invocation |
| Payload is in the queue before notification | Producers call the notifier *after* the queue push (sub buffer / PAIR queue / fired_counts + signaler) |
| No missed wake-up | Level-triggered — the worker drains each queue until the matching pull API returns `ZLINK_RECV_NO_DATA`; a redundant notification during drain is harmless |
| Direct handler vs dispatch event | Compile-time separation: `spot_sub_handler_adapter` for subscribe, `request_handler` slot for routed, timer's own handler slot for timers. Registration-time mutex in the routed axis rejects double-install. Per-timer precedence for timers is decided inside `scheduler_fire_timer` |
| Callback may call zlink API | Producers release their internal locks before invoking the notifier |

### 10.6 Why this is a single-writer design from the app's perspective

With three internal producers but a single handler, the application can
treat the notification as a condition-variable wake and then drive all
three queues from one application thread:

- No user-owned lock is needed between sub / routed / timer consumers;
  they read *different* underlying queues, so the only shared state is
  application bookkeeping that the single worker thread owns outright.
- The handler itself can be `lock_guard + cv.notify + bitmask |=`; it
  does not need to touch any zlink API.
- The producer threads never wait for the handler to finish beyond the
  raw function call — they immediately return to their polling loops,
  so slow consumer threads cannot back-pressure the SpotNode mesh or
  the timer scheduler past the configured HWMs.

This is the core reason the user-facing guide recommends
`zlink_spot_dispatch_event_handler` as the unified consumption pattern
when timer, routed recv, and subscribe all live on one Spot handle.
