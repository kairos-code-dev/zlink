[English](spot-internals.md) | [한국어](spot-internals.ko.md)

# SPOT / SpotNode 내부 아키텍처

## 1. 컴포넌트 개요

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
        handles["handle management<br/>pub/sub 생성"]
        control_task["control_task (10ms)<br/>구독 replay,<br/>ready refresh"]
    end

    subgraph RuntimeLayer["Runtime (spot_runtime_t)"]
        runtime["spot_runtime_t<br/>socket container,<br/>batch config, HWM config"]
        attachments["attachment map<br/>(pub/sub sockets)"]
    end

    subgraph DataPlaneLayer["Data Plane (별도 스레드)"]
        dp_loop["spot_data_plane_loop_t<br/>main polling loop<br/>(7개 소켓 poll)"]
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

## 2. 내부 소켓 토폴로지

SpotNode는 10개의 내부 소켓을 inproc endpoint로 연결하고,
연결 상태 추적용 monitor 소켓 1개를 추가로 생성한다 (총 11개).

### 2.1 소켓 목록

```mermaid
flowchart LR
    subgraph PubSide["Publisher 측"]
        spot_pub["spot_pub_t<br/>(PUB socket)"]
    end

    subgraph Internal["Data Plane 소켓"]
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

    subgraph SubSide["Subscriber 측"]
        spot_sub["spot_sub_t<br/>(SUB socket)"]
    end

    subgraph Remote["원격 Peer"]
        remote_node["다른 SpotNode"]
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

### 2.2 소켓 상세

| 소켓 | 타입 | Endpoint | Bind/Connect | HWM | 역할 |
|------|------|----------|-------------|-----|------|
| `ingress` | SUB | `.pub-in` | BIND | `node_sub_rcvhwm` | 모든 로컬 publish 수신 |
| `fanout` | PUB | `.sub-out` | BIND | `node_pub_sndhwm` | 로컬 subscriber에게 분배 |
| `mesh_pub` | PUB | (bound endpoint) | BIND | `node_pub_sndhwm` | 원격 peer에 토픽 송신 |
| `mesh_xsub` | XSUB | — | CONNECT to peers | `node_sub_rcvhwm` | 원격 peer에서 토픽 수신 |
| `route_ingress` | ROUTER | `.route-in` | BIND | `routed_recv_hwm` | app에서 routed 메시지 수신 |
| `node_router` | ROUTER | `.node-router` | BIND | `routed_send/recv_hwm` | app에 routed 메시지 전달 |
| `ctrl` | PAIR | `.ctrl` | CONNECT | — | control plane ↔ data plane 명령 |
| `peer_ctrl_pub` | PUB | (derived from bound) | BIND | 1024 | peer에 제어 메시지 송신 |
| `peer_ctrl_sub` | SUB | — | CONNECT to peers | 1024 | peer에서 제어 메시지 수신 |
| `mesh_xsub_monitor` | Monitor | — | — | — | CONNECTION_READY/DISCONNECTED 추적 |

모든 inproc endpoint 패턴: `inproc://zlink.spot.{node_id}.{suffix}`

### 2.3 공통 소켓 설정

모든 data plane 소켓 공통:
- `LINGER = 0`
- `SNDTIMEO = -1` (blocking)
- `ingress`: `SUBSCRIBE = ""` (모든 토픽 수신)
- `fanout`: `XPUB_NODROP = 1` (slow subscriber에 드롭하지 않음; 내부 PUB 소켓에 적용)
- `peer_ctrl_sub`: `SUBSCRIBE = "__zlink.spot.ctrl."` (제어 접두어만)

## 3. Pub/Sub Attachment

사용자 facing `spot_pub_t`와 `spot_sub_t`는 별도 attachment 소켓으로 data plane에 연결한다.

```mermaid
sequenceDiagram
    participant App as Application
    participant Pub as spot_pub_t (PUB)
    participant Ingress as ingress (SUB)
    participant DP as Data Plane
    participant Fanout as fanout (XPUB)
    participant Sub as spot_sub_t (SUB)

    Note over Pub,Ingress: PUB이 .pub-in에 connect (SUB이 bind)
    Note over Fanout,Sub: XPUB이 .sub-out에 bind (SUB이 connect)

    App->>Pub: zlink_publish(spot, topic, parts)
    Pub->>Ingress: inproc으로 전송
    Ingress->>DP: poller 깨어남 → recv
    DP->>Fanout: 로컬 fanout (즉시)
    Fanout->>Sub: 매칭되는 subscriber에 전달
    Sub->>App: zlink_subscribe() 또는 subscribe_handler callback
```

### Attachment 생성

```mermaid
sequenceDiagram
    participant Node as spot_node_t
    participant RT as spot_runtime_t
    participant PUB as New PUB Socket
    participant SUB as New SUB Socket

    Node->>RT: create_attachment(pub, pub_ingress_endpoint)
    RT->>PUB: PUB 소켓 생성
    RT->>PUB: connect("inproc://zlink.spot.{id}.pub-in")
    RT->>RT: attachment_map에 저장

    Node->>RT: create_attachment(sub, sub_fanout_endpoint)
    RT->>SUB: SUB 소켓 생성
    RT->>SUB: connect("inproc://zlink.spot.{id}.sub-out")
    RT->>RT: attachment_map에 저장
```

## 4. 토픽 메시지 흐름 (상세)

### 4.1 로컬 Publish → 로컬 Subscribe

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
    DP->>DP: has_local_filtered_subs 확인
    DP->>Fanout: send [topic] + [payload]
    Note over Fanout: XPUB이 구독 매칭
    Fanout->>Sub: 매칭되는 sub에 전달
```

### 4.2 로컬 Publish → 원격 Peer

```mermaid
sequenceDiagram
    participant Pub as spot_pub_t
    participant Ingress as ingress (SUB)
    participant DP as data_plane_loop
    participant MeshPub as mesh_pub (PUB)
    participant Remote as 원격 SpotNode

    Pub->>Ingress: [topic] + [payload parts]
    Note over DP: poller → ingress readable
    DP->>DP: recv_and_forward_ingress()

    alt Batching 활성
        DP->>DP: topic bucket에 축적
        Note over DP: flush 조건:<br/>delay_ms (20ms),<br/>max_messages (32),<br/>max_bytes (64KB)
        DP->>MeshPub: batch frame 전송 [header+metadata+body]
    else Batching 비활성 또는 bypass (msg >= 64KB)
        DP->>MeshPub: [topic] + [payload] 즉시 전송
    end

    MeshPub->>Remote: tcp/tls mesh 경유
```

### 4.3 원격 수신 → 로컬 Subscribe

```mermaid
sequenceDiagram
    participant Remote as 원격 SpotNode
    participant MeshXSub as mesh_xsub (XSUB)
    participant DP as data_plane_loop
    participant Fanout as fanout (XPUB)
    participant Sub as spot_sub_t

    Remote->>MeshXSub: tcp mesh 경유 토픽 메시지
    Note over DP: poller → mesh_xsub readable
    DP->>DP: recv_and_dispatch_mesh_xsub()
    alt Batch frame 감지 (magic=0x31544253)
        DP->>DP: unbatch → 개별 logical message
    end
    DP->>Fanout: 로컬 fanout으로 포워딩
    Fanout->>Sub: 매칭되는 sub에 전달
    Note over DP: mesh_pub으로 재발행 금지<br/>(루프 방지)
```

## 5. Routed 메시지 흐름 (상세)

### 5.1 로컬 spot → 로컬 spot (같은 노드)

```mermaid
sequenceDiagram
    participant Sender as spot_send_spot()
    participant RouteIn as route_ingress (ROUTER)
    participant DP as data_plane_loop
    participant NodeRouter as node_router (ROUTER)
    participant Receiver as spot_handler / spot_recv

    Sender->>RouteIn: [SPOT envelope 8 parts] + [payload]
    Note over RouteIn: ROUTER가 sender routing_id 추가
    Note over DP: poller → route_ingress readable
    DP->>DP: SPOT envelope 파싱
    DP->>DP: destination = 로컬 spot
    DP->>NodeRouter: [SPOT envelope] + [payload] 포워딩
    Note over NodeRouter: ROUTER가 대상 spot_rid로 라우팅
    NodeRouter->>Receiver: handler 또는 recv 큐로 전달
```

### 5.2 로컬 spot → 원격 spot (노드 간)

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
    DP1->>DP1: envelope 파싱 → dest_node = Node 2
    DP1->>Net: peer ROUTER 연결로 포워딩
    Net->>DP2: Node 2 data plane에 전달
    DP2->>DP2: envelope 파싱 → 로컬 spot 대상
    DP2->>NodeRouter2: 로컬 node_router로 포워딩
    NodeRouter2->>Receiver: 대상 spot에 전달
```

### 5.3 spot → router / router → spot

```mermaid
sequenceDiagram
    participant Spot as spot_send_router()
    participant RouteIn as route_ingress (ROUTER)
    participant DP as Data Plane
    participant Peer as ROUTER peer (transport)

    Spot->>RouteIn: [SPOT envelope: dest_class=router] + [payload]
    DP->>DP: 파싱 → destination이 ROUTER peer
    DP->>Peer: transport routing_id로 포워딩
```

## 6. Control Plane

### 6.1 Control Task 주기 (10ms)

```mermaid
flowchart TD
    start["control_task tick (10ms)"] --> replay["구독 replay<br/>(exponential holdoff)"]
    replay --> ready["구독 ready<br/>상태 갱신"]
    ready --> pub_ready["publisher<br/>delivery ready 갱신"]
    pub_ready --> peer_sync["peer state<br/>동기화"]
    peer_sync --> bootstrap["bootstrap<br/>descriptor 발행<br/>(필요 시)"]
    bootstrap --> done["완료"]
```

### 6.2 Peer 제어 메시지

Peer 제어 endpoint는 바인드된 데이터 endpoint에서 파생된다:

| Transport | Data Endpoint | Control Endpoint |
|-----------|--------------|-----------------|
| tcp | `tcp://host:9000` | `tcp://host:10000` (port+1000) |
| tls | `tls://host:9000` | `tls://host:10000` |
| ipc | `ipc:///path` | `ipc:///path.zlink-spot-ctrl.{id}` |
| inproc | `inproc://name` | `inproc://zlink.spot.peer-ctrl.{id}` |

제어 메시지 접두어:
- `__zlink.spot.ctrl.snapshot` — 상태 스냅샷
- `__zlink.spot.ctrl.ready_ack` — 구독 준비 확인
- `__zlink.spot.bootstrap.ctrl_descriptor` — bootstrap 정보

## 7. Data Plane Polling Loop

Data plane은 별도 스레드에서 실행되며 7개 소켓을 poll한다:

```mermaid
flowchart LR
    subgraph Poller["spot_data_plane_loop (7개 소켓)"]
        ctrl_poll["ctrl (PAIR)"]
        ingress_poll["ingress (SUB)"]
        mesh_poll["mesh_xsub (XSUB)"]
        peer_poll["peer_ctrl_sub (SUB)"]
        route_poll["route_ingress (ROUTER)"]
        node_poll["node_router (ROUTER)"]
        mon_poll["mesh_xsub_monitor"]
    end

    ctrl_poll -->|"명령 처리"| process_ctrl
    ingress_poll -->|"로컬 토픽"| forward_to_fanout_and_mesh
    mesh_poll -->|"원격 토픽"| forward_to_fanout
    peer_poll -->|"제어 메시지"| process_ctrl_messages
    route_poll -->|"routed 수신"| process_route_ingress
    node_poll -->|"routed 전달"| process_node_router
    mon_poll -->|"연결 이벤트"| update_peer_state
```

## 8. Unified Handle (spot_handle_t)

```cpp
struct spot_handle_t {
    uint32_t tag;                                // 검증 태그
    spot_node_t *node;                           // 부모 SpotNode
    spot_pub_t *pub;                             // Publisher (inproc PUB → ingress)
    spot_sub_t *sub;                             // Subscriber (inproc SUB ← fanout)
    zlink_subscribe_handler_fn handler;          // 토픽 callback
    void *handler_userdata;
    spot_node_t::pub_defaults_t pending_pub_defaults;
    spot_node_t::sub_defaults_t pending_sub_defaults;
    service_mode_state_t mode_state;             // recv/callback 모드 추적
    std::shared_ptr<void> request_reply_state;   // handle별 RR state
};
```

Unified handle은 SpotNode를 빌린다. 여러 handle이 하나의 node를 공유할 수 있다.
각 handle은 자신만의 pub/sub 쌍, mode state, request-reply state를 갖는다.

## 9. HWM 경계

```text
+------------------------------------------------------------------+
|  Spot Handle HWM                                                  |
|  (public facade pub/sub 소켓)                                      |
|  ┌──────────────────────────────────────────────────────────────┐ |
|  │  SpotNode Data-Plane HWM                                     │ |
|  │  ┌─────────────────────────┬────────────────────────────┐    │ |
|  │  │  SNDHWM 적용 대상:       │  RCVHWM 적용 대상:          │    │ |
|  │  │  - fanout (PUB)         │  - ingress (SUB)            │    │ |
|  │  │  - mesh_pub (PUB)       │  - mesh_xsub (XSUB)        │    │ |
|  │  │  - node_router (SND)    │  - route_ingress (ROUTER)   │    │ |
|  │  │                         │  - node_router (RCV)        │    │ |
|  │  └─────────────────────────┴────────────────────────────┘    │ |
|  │                                                               �� |
|  │  peer_ctrl은 CONTROL PLANE → 별도 HWM (1024)                 │ |
|  └──────────────────────────────────────────────────────────────┘ |
+------------------------------------------------------------------+
```

기본 내부 data-plane HWM: `1000`

Topic과 routed HWM은 `zlink_set_spot_node_option()`으로 독립 설정 가능:
- `ZLINK_SPOT_NODE_OPT_TOPIC_SNDHWM` / `ZLINK_SPOT_NODE_OPT_TOPIC_RCVHWM`
- `ZLINK_SPOT_NODE_OPT_ROUTED_SNDHWM` / `ZLINK_SPOT_NODE_OPT_ROUTED_RCVHWM`
