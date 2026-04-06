[English](services-internals.md) | [한국어](services-internals.ko.md)

# 서비스 계층 내부 설계

## 1. 개요

zlink 서비스 계층은 Discovery와 SPOT 두 가지 고수준 서비스를 제공한다.
이 문서는 내부 구현 상세를 다룬다.

SPOT에서 transport security 소유권은 의도적으로 좁게 유지한다.
`SpotNode`가 mesh/control 소켓의 TLS/WSS wiring을 책임지고, unified `Spot`은
빌린 data-plane facade로만 남는다. facade는 node lifecycle을 소유하지
않으며, 그 자체가 TLS 설정 surface는 아니다.

## 2. Registry 내부 구현

### 2.1 데이터 구조

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

### 2.2 Registry 상태 머신

```mermaid
stateDiagram-v2
    [*] --> INIT
    INIT --> RUNNING : start()
    RUNNING --> STOPPED : stop()
    STOPPED --> [*]
```

### 2.3 SERVICE_LIST 브로드캐스트 트리거

| 트리거 | 설명 |
|--------|------|
| 등록 | 서비스 REGISTER 성공 후 |
| 해제 | UNREGISTER 또는 Heartbeat 타임아웃 |
| 주기적 | 30초 (기본, 설정 가능) |

### 2.4 클러스터 동기화
- 각 Registry는 다른 Registry의 PUB를 SUB으로 구독
- flooding 방식으로 즉시 전파
- registry_id + list_seq로 중복/역전 무시

## 3. Discovery 내부 구현

### 3.1 상태 머신 (서비스별)

```mermaid
stateDiagram-v2
    [*] --> EMPTY
    EMPTY --> AVAILABLE : SERVICE_LIST (count > 0)
    AVAILABLE --> UNAVAILABLE : SERVICE_LIST (count == 0)
    UNAVAILABLE --> AVAILABLE : SERVICE_LIST (count > 0)
```

### 3.2 서비스 타입과 역할

Discovery는 프로바이더를 (service_type, service_role) 쌍으로 추적한다:

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

SPOT은 서비스 타입에서 파생되는 고정 역할을 가진다. 소켓 패밀리
서비스는 소켓 타입에 맞는 명시적 역할이 필요하다. 피어 발견을 위한 역할
매칭 규칙:
- PUB ↔ SUB
- ROUTER ↔ ROUTER, ROUTER ↔ DEALER, DEALER ↔ DEALER
- SPOT ↔ SPOT

### 3.3 Discovery 소유 서비스 실행

Discovery는 연결된 서비스의 lifecycle owner 역할을 한다. 각 서비스 타입은
`discovery_owned_service` 편의 API를 통해 엔드포인트를 등록한다:

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

Discovery는 내부적으로 `(service_type, service_role, service_name,
endpoint)` 키의 `_registered_services` 맵을 유지하고,
`refresh_registered_service_heartbeats()`로 등록된 모든 서비스의
heartbeat를 주기적으로 갱신한다.

### 3.4 소켓 Discovery 연결

`socket_discovery_attachment_t`는 raw 소켓 lifecycle을 Discovery와
통합한다. 소켓이 연결되면:

1. 소켓 타입 지원 여부 검증 (ROUTER/DEALER/PUB/SUB)
2. 소켓 타입에서 서비스 역할 파생
3. `discovery_owned_service`를 통해 소켓의 bind 엔드포인트 등록
4. 서비스 목록 업데이트를 관찰하고 피어 연결 갱신
5. 토폴로지 상태 변경을 Discovery에 보고
6. 수동 connect/disconnect/unbind/close 차단

### 3.5 구독 동작
- Registry PUB 전체 구독 (네트워크 필터링 없음)
- subscribe/unsubscribe는 내부 필터로 동작

### 3.6 중복/역전 처리
- (registry_id, list_seq) 기준 최신 스냅샷만 적용
- 동일 registry_id에서 이전 list_seq는 무시

## 4. 메시지 프로토콜

### 4.1 프레임 구조
```
Frame 0: msgId (uint16_t)
Frame 1~N: Payload (variable)
```

### 4.2 메시지 타입

| msgId | 이름 | 방향 |
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

#### 등록 및 하트비트 흐름

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

### 4.3 SERVICE_LIST 포맷
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

## 5. SPOT 내부 구현

### 5.1 구조
- `spot_node_t` -- 네트워크 제어
  - PUB/SUB 소켓 소유, mesh 관리, worker 스레드
- `spot_pub_t` -- 발행 핸들
  - spot_node_t의 publish 위임, tag 기반 유효성 검증
- `spot_sub_t` -- 구독/수신 핸들
  - 내부 큐, 패턴 매칭, 조건변수 기반 blocking recv

### 5.2 동시성 모델
- 발행: 호출자 스레드에서 직접 수행,
  `_publish_sync` mutex로 직렬화 (thread-safe)
- 수신: worker 스레드가 SUB 소켓에서 수신,
  spot_sub_t 내부 큐로 분배
- 잠금 순서: `_sync` → `_publish_sync` (데드락 방지)
- 비동기 큐 없이 직접 발행 (publish path에 메시지 버퍼링 없음)

### 5.3 구독 집계
- refcount 기반 SUB 필터 관리
- 동일 토픽의 중복 구독 시 refcount 증가
- spot_sub_t별 구독 셋 관리 (정확한 토픽 + 패턴 별도)

### 5.4 전달 정책
- 로컬 publish (spot_pub):
  로컬 spot_sub 분배 + PUB 송출 (원격 전파)
- 원격 수신 (SUB):
  로컬 spot_sub 분배만 (재발행 없음, 루프 방지)

### 5.5 Raw 소켓 정책
- `spot_pub_t`: raw PUB socket 노출하지 않음
  (thread-safety 우회 방지)
- `spot_sub_t`: raw SUB socket 노출하지 않음;
  callback/recv API로만 소비

### 5.6 Discovery 타입 분리
- service_type 필드로 spot_node/socket_family 분리
  - `service_type_spot_node` (2), `service_type_socket` (3)
- 소켓 패밀리 서비스는 추가로 `service_role` 필드를 가진다
  (ROUTER=3, DEALER=4, PUB=5, SUB=6) — 역할 기반 피어 매칭용
- 역할 매칭은 `service_roles_match()`가 강제한다 — PUB은 SUB과 짝,
  ROUTER/DEALER는 서로 짝을 이룬다
