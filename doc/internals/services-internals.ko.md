# 서비스 계층 내부 설계

## 1. 개요

zlink 서비스 계층은 Discovery, Gateway, SPOT 세 가지 고수준 서비스를 제공한다. 이 문서는 내부 구현 상세를 다룬다.

## 2. Registry 내부 구현

### 2.1 데이터 구조

```cpp
struct service_entry_t {
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
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

```
[INIT] → start() → [RUNNING] → stop() → [STOPPED]
```

### 2.3 SERVICE_LIST 브로드캐스트 트리거
| 트리거 | 설명 |
|--------|------|
| 등록 | Receiver REGISTER 성공 후 |
| 해제 | UNREGISTER 또는 Heartbeat 타임아웃 |
| 주기적 | 30초 (기본, 설정 가능) |

### 2.4 클러스터 동기화
- 각 Registry는 다른 Registry의 PUB를 SUB으로 구독
- flooding 방식으로 즉시 전파
- registry_id + list_seq로 중복/역전 무시

## 3. Discovery 내부 구현

### 3.1 상태 머신 (서비스별)
```
[EMPTY] → SERVICE_LIST(count>0) → [AVAILABLE]
[AVAILABLE] → SERVICE_LIST(count==0) → [UNAVAILABLE]
```

### 3.2 구독 동작
- Registry PUB 전체 구독 (네트워크 필터링 없음)
- subscribe/unsubscribe는 내부 필터로 동작
- Gateway 알림/조회 대상만 제한

### 3.3 중복/역전 처리
- (registry_id, list_seq) 기준 최신 스냅샷만 적용
- 동일 registry_id에서 이전 list_seq는 무시

## 4. Gateway 내부 구현

### 4.1 상태 머신 (서비스별)
```
[NO_POOL] → RECEIVER_ADDED → [POOL_READY]
[POOL_READY] → last RECEIVER_REMOVED → [NO_POOL]
```

### 4.2 서비스 풀 구조
- 서비스별 ROUTER 소켓 1개
- 모든 Receiver endpoint에 connect
- routing_id 기반 대상 지정

### 4.3 요청-응답 매핑
- request_id (uint64_t) 자동 생성
- pending_requests 맵에 저장
- 응답 수신 시 request_id로 매핑

## 5. Receiver 내부 구현

> **참고**: 공개 C API는 `zlink_receiver_*`로 명명되어 있으나, 내부 C++ 구현 클래스는
> `provider_t` (`core/src/services/gateway/receiver.hpp`)로 유지되고 있다.

### 5.1 상태 머신
```
[INIT] → bind() → [BOUND] → connect_registry() → [CONNECTED]
→ register() → [REGISTERED] → heartbeat → [REGISTERED]
→ unregister()/timeout → [UNREGISTERED]
```

### 5.2 Receiver 식별
- 기본 키: service_name + advertise_endpoint
- 동일 키 재등록 시 갱신 (routing_id/weight/heartbeat)

### 5.3 Registry Failover
- 단일 활성 Registry + 장애 시 재등록
- 즉시 시도, 연속 실패 시 지수 백오프 (200ms~5s, ±20% 지터)
- 라운드로빈 Registry 순회

## 6. 메시지 프로토콜

### 6.1 프레임 구조
```
Frame 0: msgId (uint16_t)
Frame 1~N: Payload (가변)
```

### 6.2 메시지 타입
| msgId | 이름 | 방향 |
|-------|------|------|
| 0x0001 | REGISTER | Receiver → Registry |
| 0x0002 | REGISTER_ACK | Registry → Receiver |
| 0x0003 | UNREGISTER | Receiver → Registry |
| 0x0004 | HEARTBEAT | Receiver → Registry |
| 0x0005 | SERVICE_LIST | Registry → Discovery |
| 0x0006 | REGISTRY_SYNC | Registry → Registry |
| 0x0007 | UPDATE_WEIGHT | Receiver → Registry |

### 6.3 SERVICE_LIST 포맷
```
Frame 0: msgId = 0x0005
Frame 1: registry_id (uint32_t)
Frame 2: list_seq (uint64_t)
Frame 3: service_count (uint32_t)
Frame 4~N: 서비스 엔트리
  - service_name (string)
  - receiver_count (uint32_t)
  - receiver entries: endpoint, routing_id, weight
```

### 6.4 비즈니스 메시지 (Gateway ↔ Receiver)
```
Frame 0: routing_id
Frame 1: request_id (uint64_t)
Frame 2: msgId (uint16_t)
Frame 3~N: Payload
```

## 7. SPOT 내부 구현

### 7.1 구조
- `spot_node_t` — 네트워크 제어 (PUB/SUB 소켓 소유, mesh 관리, worker 스레드)
- `spot_pub_t` — 발행 핸들 (spot_node_t의 publish 위임, tag 기반 유효성 검증)
- `spot_sub_t` — 구독/수신 핸들 (내부 큐, 패턴 매칭, 조건변수 기반 blocking recv)

### 7.2 동시성 모델
- 발행: 호출자 스레드에서 직접 수행, `_pub_sync` mutex로 직렬화 (thread-safe)
- 수신: worker 스레드가 SUB 소켓에서 수신 → spot_sub_t 내부 큐로 분배
- 잠금 순서: `_sync` → `_pub_sync` (데드락 방지)
- 비동기 큐 없이 직접 발행 (publish path에 메시지 버퍼링 없음)

### 7.3 구독 집계
- refcount 기반 SUB 필터 관리
- 동일 토픽의 중복 구독 시 refcount 증가
- spot_sub_t별 구독 셋 관리 (정확한 토픽 + 패턴 별도)

### 7.4 전달 정책
- 로컬 publish (spot_pub) → 로컬 spot_sub 분배 + PUB 송출 (원격 전파)
- 원격 수신 (SUB) → 로컬 spot_sub 분배만 (재발행 없음, 루프 방지)

### 7.5 Raw 소켓 정책
- `spot_pub_t`: raw PUB socket 노출하지 않음 (thread-safety 우회 방지)
- `spot_sub_t`: raw SUB socket 노출 (`zlink_spot_sub_socket()`, 진단/고급 용도)

### 7.4 Discovery 타입 분리
- service_type 필드로 gateway_receiver/spot_node 분리
  - `ZLINK_SERVICE_TYPE_GATEWAY` (1), `ZLINK_SERVICE_TYPE_SPOT` (2)
- 상세: [plan/type-segmentation.md](../plan/type-segmentation.md)
