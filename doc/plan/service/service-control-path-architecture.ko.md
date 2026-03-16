# Service Control Path & Monitoring Event Architecture

> `gateway`와 `spot`의 내부 소켓 토폴로지, control path 흐름,
> monitoring event 발행 구조를 한눈에 볼 수 있도록 정리한 문서다.

---

## 1. 전체 비교 요약

| 항목 | Gateway | SPOT |
| --- | --- | --- |
| 내부 소켓 수 | 2 (router + monitor) | 8 (ctrl pair + mesh + peer ctrl + ingress + fanout) |
| control path 전달 | 직접 호출 (mutex 보호) | PAIR 소켓 ASCII command protocol |
| data plane thread | 없음 (refresh task만 존재) | 전용 thread (`data_plane_t::run()`) |
| send lock 분리 | `_send_sync` (send 전용) | data plane thread 내부 처리 |
| monitoring event 발행 | socket monitor bridge + hub | socket monitor bridge + hub |
| discovery 통합 | observer pattern + refresh task | observer pattern + control task (10ms tick) |
| peer 관리 | pool 기반 routing (RR/weighted) | mesh overlay + bootstrap descriptor |
| 종료 경로 | endpoint term → socket close → drain | data plane terminate command → socket close → drain |

---

## 2. Gateway 아키텍처

### 2.1 내부 소켓 토폴로지

```
                          ┌─────────────────────────────────────┐
                          │           gateway_t                 │
                          │                                     │
  user send/send_rid ───▶ │  ┌──────────────────┐              │
                          │  │  ROUTER socket    │◄── bind(ep)  │
  provider msg ─────────▶ │  │  (data + control) │──▶ connect() │
                          │  └────────┬─────────┘              │
                          │           │ monitor bridge          │
                          │           ▼                         │
                          │  ┌──────────────────┐              │
                          │  │  PAIR socket      │              │
                          │  │  (monitor events) │              │
                          │  └────────┬─────────┘              │
                          │           │                         │
                          │           ▼                         │
                          │  ┌──────────────────┐              │
                          │  │  monitor hub      │──▶ watcher   │
                          │  └──────────────────┘    sockets    │
                          └─────────────────────────────────────┘
```

| 소켓 | 타입 | 역할 |
| --- | --- | --- |
| `router_socket` | `ZLINK_ROUTER` | data + control 겸용. bind/connect 대상, send/recv 경로 |
| `monitor_socket` | `PAIR` | router의 CONNECTION_READY / DISCONNECTED 이벤트 수신 |

### 2.2 Lock 구조

```
gateway_t
├── _public_api    ← lifecycle admission gate (atomic)
├── _sync          ← control path 전체 (pool, discovery, options, state)
└── _send_sync     ← send 전용 직렬화 (control lock과 분리)
```

**send path의 lock 분리:**

```
send():
  _public_api.enter()
  _sync.lock()        ← pool에서 provider 선택, routing_id 추출
  _sync.unlock()
  _send_sync.lock()   ← 실제 socket I/O만 직렬화
    router_socket->send(...)
  _send_sync.unlock()
```

**dispatch context 최적화 (send_rid):**

```
send_rid() in message handler context:
  routing_id == inbound source?
    → dispatch pipe로 zero-copy 전송 (lock 없음)
  그 외:
    → 일반 send 경로
```

### 2.3 Control Path 흐름

모든 control 조작은 `_sync` lock 안에서 상태를 변경하고, 필요 시
refresh task를 깨워 실제 소켓 연결/해제를 수행한다.

```
┌────────────────────────────────────────────────────────────────┐
│ Public API                                                     │
│                                                                │
│  bind(ep)              connect(ep, rid)     attach_discovery() │
│  disconnect(ep)        set_option()         set_routing_id()   │
│  set_lb_strategy()     set_send_ready_handler()                │
│     │                      │                    │              │
│     ▼                      ▼                    ▼              │
│  ┌──────────────────────────────────────────────────────┐      │
│  │  _sync lock                                          │      │
│  │  - state validation (facade mode, routing_id lock)   │      │
│  │  - pool dirty marking                                │      │
│  │  - pending_updates 등록                              │      │
│  └──────────────┬───────────────────────────────────────┘      │
│                 │                                              │
│                 ▼                                              │
│  ┌──────────────────────────────────────────────────┐          │
│  │  refresh_tick()  (주기: 1ms, service control rt) │          │
│  │                                                  │          │
│  │  1. process_monitor_events()                     │          │
│  │     - monitor socket poll (최대 64건/cycle)      │          │
│  │     - CONNECTION_READY → ready_endpoints 추가    │          │
│  │     - DISCONNECTED → down_endpoints + 500ms 백오프│          │
│  │                                                  │          │
│  │  2. dirty pool마다 refresh_pool()                │          │
│  │     - discovery/manual route에서 endpoint 수집    │          │
│  │     - 신규: setsockopt(CONNECT_ROUTING_ID) →     │          │
│  │            connect() → inflight 등록             │          │
│  │     - stale: term_endpoint() → 제거              │          │
│  │     - inflight → get_peer_state() →              │          │
│  │            POLLOUT이면 ready로 승격              │          │
│  │                                                  │          │
│  │  3. sync_gateway_peer_reports() (매 ~1초)        │          │
│  │     - registry에 peer summary upsert             │          │
│  └──────────────────────────────────────────────────┘          │
└────────────────────────────────────────────────────────────────┘
```

### 2.4 Monitoring Event 발행

```
router socket
  │
  │ (inproc monitor bridge)
  ▼
monitor_socket (PAIR)
  │
  │ process_monitor_events() (refresh_tick 안에서)
  ▼
┌───────────────────────────────────────────────────────────┐
│ Event 판정                                                │
│                                                           │
│ CONNECTION_READY on endpoint:                             │
│   → inflight → ready 승격                                 │
│   → ZLINK_GATEWAY_ROUTE_UP (ready_count 포함)            │
│   → 첫 provider면 ZLINK_GATEWAY_SEND_READY_CHANGED       │
│                                                           │
│ DISCONNECTED / HANDSHAKE_FAILED:                          │
│   → ready → down (500ms backoff)                          │
│   → pool dirty → refresh 예약                             │
│   → ZLINK_GATEWAY_ROUTE_DOWN                              │
│   → 마지막 provider면 SEND_READY_CHANGED (count=0)        │
└──────────────────────┬────────────────────────────────────┘
                       │
                       ▼
              service_monitor_hub_t
                       │
                       ├──▶ dispatch thread
                       │      │
                       │      ▼
                       │    watcher PAIR sockets ──▶ user monitor_recv()
                       │
                       └──▶ report_topology() → discovery registry
```

**Gateway Event 목록:**

| Event | 발생 조건 |
| --- | --- |
| `GATEWAY_SERVICE_READY` | 첫 bind + discovery attached |
| `GATEWAY_SERVICE_LOST` | service unregistered / destroy |
| `GATEWAY_ROUTE_UP` | endpoint connected + POLLOUT |
| `GATEWAY_ROUTE_DOWN` | endpoint disconnected / handshake fail |
| `GATEWAY_SEND_READY_CHANGED` | 첫/마지막 provider 가용성 변화 |
| `MONITOR_EVENT_CLOSED` | destroy 완료 시 terminal event |

---

## 3. SPOT 아키텍처

### 3.1 내부 소켓 토폴로지

```
┌─────────────────────────────────────────────────────────────────────┐
│                          spot_node_t                                │
│                                                                     │
│  ┌──────────────┐         inproc ctrl          ┌──────────────────┐ │
│  │data_ctrl_front├────────────────────────────▶│data_ctrl_back    │ │
│  │  (PAIR)       │                             │  (PAIR)          │ │
│  └──────────────┘                              └────────┬─────────┘ │
│   main thread side                                      │           │
│                                              data plane thread      │
│                                                         │           │
│  ┌──────────────────────────────────────────────────────┼─────────┐ │
│  │  Data Plane                                          │         │ │
│  │                                                      ▼         │ │
│  │  ┌────────────┐    bind(pub ep)    ┌──────────────────┐        │ │
│  │  │ mesh_pub   ├───────────────────▶│  peer network    │        │ │
│  │  │ (PUB)      │                    │  (다른 node)     │        │ │
│  │  └────────────┘                    └────────┬─────────┘        │ │
│  │                                             │                  │ │
│  │  ┌────────────┐    connect(peer)            │                  │ │
│  │  │ mesh_xsub  │◄───────────────────────────┘                  │ │
│  │  │ (XSUB)     │                                               │ │
│  │  └────────────┘                                               │ │
│  │                                                               │ │
│  │  ┌──────────────┐  connect(peer ctrl ep)  ┌─────────────────┐ │ │
│  │  │peer_ctrl_pub ├───────────────────────▶│  peer ctrl       │ │ │
│  │  │ (PUB)        │                        │  (다른 node)     │ │ │
│  │  └──────────────┘                        └────────┬─────────┘ │ │
│  │                                                   │           │ │
│  │  ┌──────────────┐  bind(derived ep)               │           │ │
│  │  │peer_ctrl_sub │◄────────────────────────────────┘           │ │
│  │  │ (SUB)        │                                             │ │
│  │  └──────────────┘                                             │ │
│  │                                                               │ │
│  │  ┌──────────────┐  bind(inproc pub-in)                        │ │
│  │  │ ingress      │◄──── spot_pub attachment (PUB) connect      │ │
│  │  │ (SUB)        │                                             │ │
│  │  └──────────────┘                                             │ │
│  │                                                               │ │
│  │  ┌──────────────┐  bind(inproc sub-out)                       │ │
│  │  │ fanout       │────▶ spot_sub attachment (SUB) connect      │ │
│  │  │ (XPUB)       │                                             │ │
│  │  └──────────────┘                                             │ │
│  └───────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

| 소켓 | 타입 | 방향 | 역할 |
| --- | --- | --- | --- |
| `data_ctrl_front` | PAIR | main → data plane | control command 전송 (client side) |
| `data_ctrl_back` | PAIR | data plane → main | control command 수신 + 응답 (server side) |
| `mesh_pub` | PUB | outbound | peer network에 data + bootstrap descriptor 발행 |
| `mesh_xsub` | XSUB | inbound | peer network에서 data + bootstrap descriptor 수신 |
| `peer_ctrl_pub` | PUB | outbound | peer에게 subscription snapshot 발행 |
| `peer_ctrl_sub` | SUB | inbound | peer로부터 subscription snapshot 수신 |
| `ingress` | SUB | local inbound | local spot_pub attachment → data plane |
| `fanout` | XPUB | local outbound | data plane → local spot_sub attachment |

### 3.2 Control Command Protocol

main thread와 data plane thread 사이의 모든 control 조작은
PAIR 소켓의 ASCII command protocol로 전달된다.

```
spot_node_t (main thread)
    │
    │ send_command(verb, arg)   ← ctrl_sync lock
    ▼
data_ctrl_front (PAIR)
    │
    │ inproc://zlink.spot.{node_id}.ctrl
    ▼
data_ctrl_back (PAIR)
    │
    │ data_plane_t::run() 안에서 poll
    ▼
command handler → reply "ok" / "error\n{errno}"
```

**Command 목록:**

| Verb | Arg | 용도 |
| --- | --- | --- |
| `bind_pub` | endpoint | mesh_pub bind + peer_ctrl_sub bind (파생 endpoint) |
| `unbind_pub` | endpoint | mesh_pub unbind + peer_ctrl_sub unbind |
| `connect_peer_pub` | peer_endpoint | mesh_xsub connect (peer_ctrl_pub는 bootstrap 후 connect) |
| `disconnect_peer_pub` | peer_endpoint | mesh_xsub + peer_ctrl_pub disconnect |
| `replay_subscriptions` | - | 모든 connected peer에 subscription snapshot 전송 |
| `subscription_subscribe` | filter | peer_ctrl_pub snapshot에 filter 추가 |
| `subscription_unsubscribe` | filter | peer_ctrl_pub snapshot에서 filter 제거 |
| `ready_ack_subscribe` | `ep\nfilter\nsource_id` | pub delivery ready tracking 등록 |
| `ready_ack_unsubscribe` | `ep\nfilter\nsource_id` | pub delivery ready tracking 해제 |
| `terminate` | - | data plane thread 정상 종료 |

### 3.3 Data Plane Event Loop

```
data_plane_t::run()
│
│  초기화: 8개 소켓 생성, bind/connect, monitor bridge
│
▼
while (running) {
│
├─ Phase 0: Pre-poll maintenance
│   pump_socket_commands(mesh_pub, mesh_xsub,
│     peer_ctrl_pub, peer_ctrl_sub, ingress, fanout)
│   set_all_pipes_nodelay() on mesh sockets
│
├─ Phase 1: Non-blocking ctrl message 선처리
│   recv_and_process_ctrl_messages(peer_ctrl_sub)
│
├─ Phase 2: Poll (20ms timeout)
│   대상: [ctrl, ingress, mesh_xsub, peer_ctrl_sub, mesh_xsub_monitor]
│
├─ Phase 3: Multi-pass event handling (우선순위별)
│   │
│   ├─ Pass 0: CONTROL
│   │   ctrl socket    → command 처리 (bind_pub, connect 등)
│   │   peer_ctrl_sub  → subscription snapshot 수신/처리
│   │   monitor bridge → CONNECTION_READY / DISCONNECTED
│   │
│   ├─ Pass 1: MESH
│   │   mesh_xsub → bootstrap descriptor 파싱
│   │              → peer_ctrl_pub connect (bootstrap 후)
│   │              → data를 fanout으로 forward
│   │
│   └─ Pass 2: INGRESS
│       ingress → local pub data 수신
│                → mesh_pub + fanout으로 forward (최대 2048/batch)
│
└─ Phase 4: Bootstrap descriptor 주기 발행
    mesh_pub에 자신의 endpoint 정보 발행
    주기: TCP/TLS 5초, inproc 1초
}
```

### 3.4 Control Path 흐름

```
┌──────────────────────────────────────────────────────────────┐
│ Public API                                                    │
│                                                               │
│  bind(ep)         connect_peer(ep)    attach_discovery()      │
│  subscribe(f)     set_option()        publish(topic, data)    │
│     │                  │                    │                 │
│     ▼                  ▼                    ▼                 │
│  ┌────────────────────────────────────────────────────────┐   │
│  │  _sync lock + _public_api guard                        │   │
│  │  - state validation                                    │   │
│  │  - send_data_plane_command(verb, arg)                  │   │
│  │    (ctrl_sync lock → data_ctrl_front → data plane)     │   │
│  └────────────────────┬───────────────────────────────────┘   │
│                       │                                       │
│                       ▼                                       │
│  ┌────────────────────────────────────────────────────────┐   │
│  │  control_tick()  (10ms 주기, service control rt)       │   │
│  │                                                        │   │
│  │  1. refresh_discovery_peers()                          │   │
│  │     - discovery에서 peer endpoint 목록 갱신            │   │
│  │     - 신규 peer → connect_peer_pub command             │   │
│  │     - stale peer → disconnect_peer_pub command         │   │
│  │                                                        │   │
│  │  2. refresh_connected_peer_endpoints()                 │   │
│  │     - mesh_xsub monitor에서 실제 연결 상태 추적        │   │
│  │     - connected_peer_version으로 변화 감지             │   │
│  │                                                        │   │
│  │  3. emit_pending_subscription_replays()                │   │
│  │     - holdoff 후 replay_subscriptions command          │   │
│  │     - transport별 holdoff: inproc 50tick, tcp 150tick  │   │
│  │                                                        │   │
│  │  4. emit_pending_subscription_ready_events()           │   │
│  │     - sub readiness event firewall                     │   │
│  │                                                        │   │
│  │  5. emit_pending_pub_delivery_ready_events()           │   │
│  │     - pub delivery readiness event firewall            │   │
│  └────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 3.5 Peer Bootstrap & Subscription 흐름

SPOT은 peer 간 연결 시 2단계 handshake를 거친다.

```
Node A                                        Node B
  │                                              │
  │  1. connect_peer_pub(B의 mesh_pub ep)        │
  │     mesh_xsub ──connect──▶ mesh_pub          │
  │                                              │
  │  2. B가 주기적으로 bootstrap descriptor 발행  │
  │     mesh_xsub ◄── bootstrap descriptor ──    │
  │     "__zlink.spot.bootstrap.ctrl_descriptor"  │
  │     [self_pub_ep, self_ctrl_ep, node_id, ver] │
  │                                              │
  │  3. A가 bootstrap 수신 후 ctrl 연결          │
  │     peer_ctrl_pub ──connect──▶ peer_ctrl_sub │
  │                                              │
  │  4. subscription snapshot 교환               │
  │     peer_ctrl_pub ──snapshot──▶ peer_ctrl_sub│
  │     "__zlink.spot.ctrl.snapshot"              │
  │     [target_ep, source_key, ver, filters...] │
  │                                              │
  │  5. 이후 data 흐름                           │
  │     ingress → mesh_pub ──data──▶ mesh_xsub   │
  │                                → fanout      │
  │                                → spot_sub    │
```

### 3.6 Monitoring Event 발행

```
spot_pub_t / spot_sub_t / spot_node_t
  │
  │ emit_monitor_event(event)
  ▼
service_monitor_hub_t
  │
  │ _dispatch_queue.push(event)
  │ _dispatch_cv.broadcast()
  ▼
dispatch_loop() [전용 dispatch thread]
  │
  │ event type → delivery_mask 계산
  │ watcher mask와 AND → 해당 watcher에만 전달
  ▼
watcher PAIR sockets ──▶ user monitor_recv()
```

**SPOT Event 목록:**

| Event | 발생 조건 | 발행 주체 |
| --- | --- | --- |
| `SPOT_PUB_DELIVERY_READY_CHANGED` | subject별 delivery count 변화 | spot_pub_t |
| `SPOT_PUB_FIRST_DELIVERY_READY_CHANGED` | 첫 delivery ready count 변화 | spot_pub_t |
| `SPOT_SUB_SUBSCRIPTION_READY` | subscription이 peer에 의해 ack됨 | spot_sub_t |
| `SPOT_SUB_DELIVERY_READY_CHANGED` | delivery ready 상태 변화 | spot_sub_t |
| `SPOT_SUB_SUBJECT_RECEIVED` | subject 메시지 수신 (monitoring 시) | spot_sub_t |
| Socket monitor events | CONNECTED, DISCONNECTED 등 | mesh socket monitor bridge |

### 3.7 Ready Ack 추적 (Pub Delivery Ready)

pub의 delivery readiness를 추적하는 양방향 프로토콜이다.

```
spot_pub (Node A)                          spot_sub (Node B)
  │                                              │
  │  ready_ack_subscribe(B_ep, filter, src_id)   │
  │  → data plane command                        │
  │                                              │
  │  outbound_ready_filters[B][src_id] += filter │
  │                                              │
  │  snapshot에 ready_ack source로 포함           │
  │  peer_ctrl_pub ──snapshot──▶ peer_ctrl_sub   │
  │                                              │
  │                  notify_pub_delivery_ready_ack│
  │  ◄────────────── (target, filter, src, true) │
  │                                              │
  │  DELIVERY_READY_CHANGED event 발행            │
```

---

## 4. 공통 인프라

### 4.1 service_monitor_hub_t

gateway와 spot 모두 동일한 monitoring hub 구조를 사용한다.

```
┌─────────────────────────────────────────────┐
│  service_monitor_hub_t                       │
│                                              │
│  _watchers[]        ← monitor handle 목록    │
│    │ watcher.server  (PAIR, hub → user)      │
│    │ watcher.client  (PAIR, user → hub)      │
│    │ watcher.mask    (event filter)          │
│                                              │
│  _dispatch_queue[]  ← pending events         │
│  _dispatch_cv       ← wakeup signal          │
│  _dispatch_thread   ← 전용 dispatch thread   │
│                                              │
│  emit(event):                                │
│    lock(_dispatch_sync)                      │
│    _dispatch_queue.push(event)               │
│    _dispatch_cv.broadcast()                  │
│                                              │
│  dispatch_loop():                            │
│    event = dequeue()                         │
│    delivery_mask = calc_mask(event)           │
│    for watcher matching mask:                │
│      watcher.server->send(event_payload)     │
└─────────────────────────────────────────────┘
```

### 4.2 service_public_api_guard_t

lifecycle admission gate로, gateway와 spot 모두 동일하게 사용한다.

```
┌──────────────────────────────────────────────┐
│  std::atomic<uint32_t> _state                │
│                                              │
│  bit 31: closing_bit (0x80000000)            │
│  bit 0-30: inflight count                    │
│                                              │
│  enter_public_api():                         │
│    closing_bit set? → ESHUTDOWN              │
│    아니면 inflight++                         │
│                                              │
│  leave_public_api():                         │
│    inflight--                                │
│                                              │
│  begin_close_or_fail_busy():                 │
│    closing_bit already set? → EALREADY       │
│    inflight > 0? → EBUSY                     │
│    CAS로 closing_bit set                     │
└──────────────────────────────────────────────┘
```

### 4.3 service_runtime_base_t

소켓 lifecycle 추적 기반이다. gateway와 spot 모두 이 위에 구축된다.

```
┌─────────────────────────────────────────────────┐
│  service_runtime_base_t                          │
│                                                  │
│  _owned_sockets     ← 활성 소켓 map (id → ptr)  │
│  _closing_sockets   ← close 진행 중 소켓 map     │
│  _sync              ← 소켓 map 보호 mutex        │
│                                                  │
│  register_socket(sock):    _owned → 추가         │
│  close_socket(sock):       _owned → _closing     │
│  close_socket_and_wait():  close + removal 대기  │
│  wait_drained(timeout):    _closing 전체 drain   │
│  on_socket_removed(id):    _closing에서 제거     │
└─────────────────────────────────────────────────┘
```

---

## 5. 종료 경로 비교

### 5.1 Gateway destroy

```
gateway_t::destroy()
│
├─ 1. _runtime->stop = 1
├─ 2. refresh task 제거
├─ 3. discovery observer 해제, service unregister
├─ 4. SERVICE_LOST event + topology report (STOPPED)
├─ 5. 모든 endpoint 수집 (bind, ready, inflight, down)
├─ 6. peer report 정리 (STOPPED)
├─ 7. runtime state 초기화
├─ 8. MONITOR_EVENT_CLOSED 발행
├─ 9. monitor socket close
├─ 10. 수집된 endpoint 전부 term_endpoint()
├─ 11. router socket close
└─ 12. wait_drained(10s)
```

### 5.2 SPOT node destroy

```
spot_node_t::destroy()
│
├─ 1. control task 제거
├─ 2. 모든 attachment에 destroy_attachment()
│     endpoint 기억 → term_endpoint()
│     → set_all_pipes_nodelay()
│     → close_socket_and_wait()
├─ 3. close_control_sockets()
│     data_ctrl_front/back, mesh_pub, mesh_xsub,
│     peer_ctrl_pub/sub, ingress, fanout
│     (각 2s timeout, close_socket_and_wait)
├─ 4. send_data_plane_command("terminate")
├─ 5. data plane thread join
├─ 6. wait_owned_socket_removals(10s)
├─ 7. 필요 시 abortive_stop()
└─ 8. force_wait_remaining(5s)
```

---

## 6. Thread 구성 비교

### 6.1 Gateway

```
┌──────────────────────────────────────┐
│  Main thread(s)                      │
│  - public API 호출 (send, bind, ...) │
│  - _public_api + _sync + _send_sync  │
└──────────────┬───────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Service control runtime thread      │
│  - refresh_tick() (1ms 주기)         │
│  - monitor event 처리                │
│  - pool refresh                      │
└──────────────────────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Monitor dispatch thread             │
│  - event queue → watcher 전달        │
└──────────────────────────────────────┘
```

### 6.2 SPOT

```
┌──────────────────────────────────────┐
│  Main thread(s)                      │
│  - public API 호출                   │
│  - _public_api + _sync               │
│  - PAIR ctrl command 전송            │
└──────────────┬───────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Service control runtime thread      │
│  - control_tick() (10ms 주기)        │
│  - discovery peer refresh            │
│  - subscription replay               │
│  - readiness event firewall          │
└──────────────────────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Data plane thread                   │
│  - 8개 내부 소켓 소유/poll           │
│  - command 처리                      │
│  - mesh forwarding                   │
│  - bootstrap descriptor 발행         │
└──────────────────────────────────────┘
               │
┌──────────────▼───────────────────────┐
│  Monitor dispatch thread             │
│  - event queue → watcher 전달        │
└──────────────────────────────────────┘
```

---

## 7. 동기화 요약

### 7.1 Gateway Mutex/Atomic

| 동기화 수단 | 보호 대상 | 사용 thread |
| --- | --- | --- |
| `_public_api` (atomic) | lifecycle admission gate | 모든 thread |
| `_sync` (mutex) | pool, discovery, options, routing state | main + refresh task |
| `_send_sync` (mutex) | router socket send I/O | main thread(s) |
| `_handler` (atomic) | message handler pointer | main + router dispatch |
| `_send_ready_handler` (atomic) | send-ready handler pointer | main + router dispatch |

### 7.2 SPOT Mutex/Atomic

| 동기화 수단 | 보호 대상 | 사용 thread |
| --- | --- | --- |
| `_public_api` (atomic) | lifecycle admission gate | 모든 thread |
| `_sync` (mutex) | node state, peer lists, discovery, options | main + control task |
| `ctrl_sync` (mutex) | data_ctrl_front command channel | main + control task |
| `attachment_sync` (mutex) | attachment map | main + attachment thread |
| `connected_peer_sync` (mutex) | connected peer set + version | data plane + control task |
| `connected_peer_version` (atomic) | peer set 변경 감지 | data plane + control task |
| `_local_filtered_sub_count` (atomic) | local filtered sub 수 | main + data plane |
| `_active_peer_count` (atomic) | active peer 수 | main + data plane |

---

## 8. HWM 설정 (SPOT 전용)

| 소켓 | 방향 | 환경변수 | 기본값 |
| --- | --- | --- | --- |
| ingress | RCV | `ZLINK_SPOT_INTERNAL_INGRESS_RCVHWM` | 8192 |
| mesh_xsub | RCV | `ZLINK_SPOT_INTERNAL_MESH_XSUB_RCVHWM` | 8192 |
| peer_ctrl_sub | RCV | `ZLINK_SPOT_INTERNAL_PEER_CTRL_RCVHWM` | 1024 |
| fanout | SND | `ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM` | 1000 |

Data plane batch 제한:

| 경로 | 최대 batch/poll |
| --- | --- |
| ingress forward | 2048 messages |
| mesh_xsub forward | 1024 messages |
| ctrl poll | 64 messages |
