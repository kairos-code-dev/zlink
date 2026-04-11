[English](discovery-internals.md) | [한국어](discovery-internals.ko.md)

# Discovery 서비스 내부 아키텍처

## 1. 컴포넌트 개요

```mermaid
flowchart TB
    subgraph PublicAPI["Public API"]
        disc_new["zlink_discovery_new()"]
        disc_connect["zlink_discovery_connect_registry()"]
        disc_destroy["zlink_discovery_destroy()"]
    end

    subgraph DiscoveryCore["discovery_t"]
        bootstrap_rt["bootstrap_runtime<br/>DEALER → Registry ROUTER"]
        uplink_rt["uplink_runtime<br/>heartbeat, topology report"]
        sub_socket["SUB socket<br/>SERVICE_LIST 수신"]
        service_state["service_state<br/>provider 스냅샷"]
        observers["observer 목록<br/>(attachment들)"]
        registered["_registered_services<br/>(service_type, role, name, endpoint)"]
        control_task["control_task (주기적)"]
    end

    subgraph Attachments["서비스 Attachment"]
        spot_attach["SpotNode attachment"]
        socket_attach["socket_discovery_attachment_t<br/>(ROUTER/DEALER/PUB/SUB)"]
    end

    subgraph Registry["Registry"]
        reg_router["ROUTER socket"]
        reg_pub["XPUB socket"]
    end

    disc_new --> DiscoveryCore
    disc_connect --> bootstrap_rt
    bootstrap_rt -->|DEALER| reg_router
    uplink_rt -->|DEALER| reg_router
    reg_pub -->|SERVICE_LIST| sub_socket
    sub_socket --> service_state
    service_state --> observers
    observers --> spot_attach
    observers --> socket_attach
    control_task --> uplink_rt
```

## 2. 소켓 종류와 Endpoint

| 소켓 | 타입 | 대상 | 용도 |
|------|------|------|------|
| Bootstrap DEALER | DEALER | Registry ROUTER | 초기 등록, bootstrap 요청 |
| Topology Report DEALER | DEALER | Registry uplink | topology 상태 보고 |
| Control DEALER | DEALER | Registry uplink | heartbeat, 속성 업데이트 |
| SERVICE_LIST SUB | SUB | Registry XPUB | 서비스 목록 브로드캐스트 수신 |

모든 DEALER 소켓은 필요 시 생성되고 shutdown 시 파괴된다.

## 3. Lifecycle 상태 머신

```mermaid
stateDiagram-v2
    [*] --> CREATED: discovery_new()
    CREATED --> BOOTSTRAPPING: connect_registry()
    BOOTSTRAPPING --> BOOTSTRAPPED: bootstrap_rep 수신
    BOOTSTRAPPED --> UPLINKED: uplink DEALER 생성
    UPLINKED --> SUBSCRIBED: SUB이 Registry PUB에 연결
    SUBSCRIBED --> RUNNING: SERVICE_LIST 수신

    RUNNING --> RUNNING: 주기적 tick<br/>(heartbeat, topology 갱신)
    RUNNING --> SHUTDOWN: destroy()
    SHUTDOWN --> [*]

    BOOTSTRAPPING --> BOOTSTRAPPING: 재시도 (timeout 2000ms)
```

## 4. Bootstrap 흐름

```mermaid
sequenceDiagram
    participant App as Application
    participant Disc as Discovery
    participant DEALER as Bootstrap DEALER
    participant REG as Registry ROUTER

    App->>Disc: connect_registry("tcp://registry:5551")
    Disc->>Disc: pending_bootstrap_endpoints에 추가
    Note over Disc: control_task tick

    Disc->>DEALER: ensure_bootstrap_dealer()
    DEALER->>REG: BOOTSTRAP_REQ (0x0008)<br/>[routing_id]
    REG->>DEALER: BOOTSTRAP_REP (0x0009)<br/>[registry_id, heartbeat_interval,<br/>pub_endpoint, uplink_endpoint]

    Disc->>Disc: registry 설정 저장
    Disc->>Disc: uplink DEALER 생성
    Disc->>Disc: SUB 소켓 생성
    Disc->>Disc: SUB을 pub_endpoint에 connect
    Note over Disc: bootstrap 완료
```

## 5. 서비스 등록 흐름

```mermaid
sequenceDiagram
    participant Service as 서비스 (SPOT/Socket)
    participant Disc as Discovery
    participant DEALER as Control DEALER
    participant REG as Registry ROUTER

    Service->>Disc: register_endpoint(type, endpoint, weight)
    Disc->>Disc: _registered_services에 저장
    Disc->>DEALER: REGISTER (0x0001)<br/>[service_type, service_name,<br/>service_role, endpoint, routing_id]
    REG->>DEALER: REGISTER_ACK (0x0002)<br/>[status, resolved_endpoint]

    loop heartbeat_interval 마다
        Disc->>DEALER: HEARTBEAT (0x0004)<br/>[service_type, service_role,<br/>service_name, endpoint]
    end
```

## 6. SERVICE_LIST 업데이트 흐름

```mermaid
sequenceDiagram
    participant REG as Registry XPUB
    participant SUB as Discovery SUB
    participant State as service_state
    participant Observer as Attachment Observer

    REG->>SUB: SERVICE_LIST (0x0005)<br/>[registry_id, list_seq,<br/>service_count, entries...]
    SUB->>State: service_type/name으로 파싱 및 필터링
    State->>State: apply_provider_snapshot()
    State->>State: provider 변경 여부 확인

    alt Provider 변경됨
        State->>Observer: on_service_update(snapshot)
        Observer->>Observer: refresh_peers()
        Note over Observer: 새 peer 연결,<br/>제거된 peer 해제
    end
```

### SERVICE_LIST 프레임 형식

```text
Frame 0: msg_id = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: 서비스 항목 (반복):
  - service_type (uint16_t)
  - service_name (string)
  - provider_count (uint32_t)
  - provider별:
      service_role (uint16_t)
      endpoint (string)
      routing_id (가변)
      value (int64_t)
      metadata (가변)
```

## 7. Socket Discovery Attachment

`socket_discovery_attachment_t`는 raw 소켓을 Discovery와 통합하여
자동 peer 관리를 수행한다.

```mermaid
sequenceDiagram
    participant Socket as Raw Socket (ROUTER)
    participant Attach as socket_discovery_attachment_t
    participant Disc as Discovery
    participant REG as Registry

    Socket->>Socket: zlink_bind("tcp://*:5555")
    Socket->>Attach: attach(socket, discovery)
    Attach->>Attach: socket_type에서 service_role 파생<br/>(ROUTER→3, DEALER→4, PUB→5, SUB→6)
    Attach->>Disc: register_endpoint(service_type_socket,<br/>endpoint, role)
    Disc->>REG: REGISTER

    Attach->>Disc: add_observer(self)
    Note over Attach: 이제 SERVICE_LIST 업데이트 수신

    Disc->>Attach: on_service_update(providers)
    Attach->>Attach: service_roles_match()로 필터링
    Attach->>Socket: zlink_connect(new_peer_endpoint)
    Attach->>Socket: zlink_disconnect(removed_peer_endpoint)
```

### 역할 매칭 규칙

| 소켓 타입 | Service Role | 매칭 대상 |
|-----------|-------------|----------|
| ROUTER | 3 | ROUTER (3), DEALER (4) |
| DEALER | 4 | ROUTER (3), DEALER (4) |
| PUB | 5 | SUB (6) |
| SUB | 6 | PUB (5) |
| SPOT | 2 | SPOT (2) |

### Attachment 제약

- 소켓당 바인드된 endpoint 1개만 허용
- 수동 `connect`/`disconnect`/`unbind` 차단 (Discovery 독점)
- Peer 연결은 Discovery가 전적으로 관리
- `discovery_destroy()` 시 모든 attachment에 shutdown 전파

## 8. SpotNode Attachment

SpotNode는 동일한 observer 패턴을 사용하지만:
- `service_type = service_type_spot_node (2)`
- `service_role = service_role_spot (2)` (고정)
- Peer 연결 대상은 mesh 내 다른 SpotNode

```mermaid
sequenceDiagram
    participant Node as SpotNode
    participant Disc as Discovery
    participant REG as Registry

    Node->>Node: zlink_spot_node_bind("tcp://*:9000")
    Node->>Disc: attach_discovery(discovery)
    Disc->>REG: REGISTER(type=spot_node, endpoint)

    Disc->>Node: on_service_update(spot_node providers)
    Node->>Node: connect_peer(new_spot_node_endpoint)
    Note over Node: mesh 자동 구성
```

## 9. Control Task 주기

```mermaid
flowchart TD
    tick["control_task tick"] --> bootstrap["pending bootstrap<br/>endpoint 확인"]
    bootstrap --> sub["SUB 소켓이<br/>PUB에 연결 확인"]
    sub --> poll["SUB에서<br/>SERVICE_LIST poll"]
    poll --> parse["서비스 업데이트<br/>파싱 및 적용"]
    parse --> heartbeat["등록된 서비스<br/>heartbeat 갱신"]
    heartbeat --> topology["topology 보고서<br/>flush"]
    topology --> notify["변경 시<br/>observer 알림"]
```

## 10. 메시지 프로토콜

| msg_id | 이름 | 방향 | 용도 |
|--------|------|------|------|
| 0x0001 | REGISTER | DEALER→ROUTER | 서비스 등록 |
| 0x0002 | REGISTER_ACK | ROUTER→DEALER | 등록 확인 |
| 0x0003 | UNREGISTER | DEALER→ROUTER | 서비스 해제 |
| 0x000D | UNREGISTER_ACK | ROUTER→DEALER | 해제 확인 |
| 0x0004 | HEARTBEAT | DEALER→ROUTER | keep-alive |
| 0x0005 | SERVICE_LIST | PUB→SUB | 서비스 브로드캐스트 |
| 0x0007 | UPDATE_ATTRIBUTES | DEALER→ROUTER | 서비스 메타데이터 업데이트 |
| 0x0008 | BOOTSTRAP_REQ | DEALER→ROUTER | 초기 설정 요청 |
| 0x0009 | BOOTSTRAP_REP | ROUTER→DEALER | 설정 응답 |
| 0x000A | TOPOLOGY_REPORT | DEALER→ROUTER | topology 상태 보고 |
| 0x000B | TOPOLOGY_QUERY | DEALER→ROUTER | 서비스 topology 조회 |
| 0x000C | TOPOLOGY_REPLY | ROUTER→DEALER | topology 조회 응답 |
