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

    alt Batching enabled
        DP->>DP: accumulate in topic bucket
        Note over DP: flush when:<br/>delay_ms (20ms),<br/>max_messages (32),<br/>max_bytes (64KB)
        DP->>MeshPub: send batch frame [header+metadata+body]
    else Batching disabled or bypass (msg >= 64KB)
        DP->>MeshPub: send [topic] + [payload] immediately
    end

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
    alt Batch frame detected (magic=0x31544253)
        DP->>DP: unbatch → individual logical messages
    end
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
