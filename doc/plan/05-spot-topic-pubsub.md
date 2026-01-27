# SPOT 토픽 PUB/SUB 스펙 (SPOT Topic PUB/SUB)

> **우선순위**: 5 (Core Feature)
> **상태**: Draft
> **버전**: 2.3
> **의존성**:
> - [00-routing-id-unification.md](00-routing-id-unification.md) (routing_id 포맷)
> - [04-service-discovery.md](04-service-discovery.md) (Registry/Discovery 연동)

## 목차
1. [개요](#1-개요)
2. [아키텍처](#2-아키텍처)
3. [토픽 모델](#3-토픽-모델)
4. [노드 동기화/라우팅](#4-노드-동기화라우팅)
5. [메시지 프로토콜](#5-메시지-프로토콜)
6. [C API 명세](#6-c-api-명세)
7. [사용 예시](#7-사용-예시)
8. [구현 계획](#8-구현-계획)
9. [검증 방법](#9-검증-방법)
10. [변경 이력](#10-변경-이력)

---

## 1. 개요

### 1.1 배경

기존 ZMQ PUB/SUB는 엔드포인트를 직접 지정해야 하므로 위치 투명성이 없다.
마이크로서비스/게임 서버 환경에서는 **토픽 이름만으로 발행/구독**할 수 있는
추상화 계층이 필요하다.

### 1.2 목표

- **위치 투명성**: 토픽 이름만으로 발행/구독
- **다중 인스턴스**: 서버/프로세스당 SPOT 인스턴스를 수백 개 생성 가능
- **No SPoF**: 단일 허브 의존 구조 금지
- **안정적 토픽 모델**: 토픽 소유권(Owner) 기반 라우팅
- **확장성**: 노드 수 증가에도 제어-plane 비용이 예측 가능

### 1.3 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Instance** | 애플리케이션이 생성하는 경량 핸들. publish/subscribe 호출 주체. |
| **SPOT Node(Agent)** | 서버/프로세스 당 1개 권장. 클러스터 통신/Discovery/라우팅 담당. |
| **OWNED Topic** | 해당 SPOT Node 내부의 어떤 SPOT 인스턴스가 **소유**한 토픽 |
| **ROUTED Topic** | 다른 SPOT Node가 소유한 토픽 (이 노드는 라우팅만 수행) |
| **Pending Subscription** | 구독 요청은 등록됐지만 **owner가 아직 없어서** 활성화되지 않은 상태 |

> **OWNED/ROUTED는 상대적 개념**이며, 토픽 소유자는 클러스터 전역에서 유일하다.

### 1.4 SPOT vs Node

- **SPOT Instance**는 사용자 코드가 직접 사용하는 객체이다.
- **SPOT Node**는 네트워크 참여자이며, 여러 SPOT Instance를 **로컬에서 multiplex**한다.
- 보통 **1 Node : N Spot** 구조를 권장한다.

---

## 2. 아키텍처

### 2.1 단일 서버 구조 (다중 SPOT 인스턴스)

```
┌────────────────────────────────────────────┐
│                  Server                    │
│                                            │
│  SPOT#1   SPOT#2   ...   SPOT#N             │
│    │        │              │               │
│    └────────┴───── inproc ─┴───────┐       │
│                                   ▼       │
│                        ┌────────────────┐  │
│                        │  SPOT Node     │  │
│                        │  (Agent)       │  │
│                        │  - Topic Map   │  │
│                        │  - Sub Map     │  │
│                        │  - ROUTER      │  │
│                        └────────────────┘  │
└────────────────────────────────────────────┘
```

- **SPOT Instance는 경량**이며, 네트워크 소켓을 직접 갖지 않는다.
- 네트워크 통신/Discovery/라우팅은 **SPOT Node**가 담당한다.

### 2.2 클러스터 구조 (No SPoF, Router-only Mesh)

```
Node A (ROUTER)  <──►  Node B (ROUTER)  <──►  Node C (ROUTER)
   ▲  ▲                          ▲                    ▲
   │  └── inproc ── SPOTs         └── inproc ── SPOTs   └── inproc ── SPOTs
```

- 모든 노드는 **ROUTER 소켓 하나로 bind+connect**를 수행한다.
- **단일 허브 의존 구조는 금지**한다.
- **노드 간 연결은 Discovery 기반 자동 연결/해제**만 지원한다.

### 2.3 데이터 흐름 (SPOT Instance 관점)

**OWNED 토픽 publish**
```
SPOT#1 (owner) -> Node -> [로컬 구독자] + [원격 구독 노드들]
```

**ROUTED 토픽 publish**
```
SPOT#2 -> Node -> (owner Node로 PUBLISH) -> owner Node -> 구독자들에게 fan-out
```

### 2.4 전파 방식 명시

- 토픽 생성/삭제 알림은 **모든 peer 노드에 fan-out**된다.
- 브로드캐스트/멀티캐스트 기능은 제공하지 않는다.
- 구현은 **peer 목록을 순회하며 routing_id를 바꿔 N회 전송**하는 방식이다.

### 2.5 Thread-safety

- **SPOT Instance는 기본적으로 thread-safe 하지 않다.**
  - 단일 스레드에서 사용 권장
  - 필요 시 `zmq_spot_new_threadsafe()` 사용
- **SPOT Node는 내부 동기화를 포함하며 thread-safe**하게 제공한다.
  - 다수의 SPOT Instance가 동시에 접근 가능

### 2.6 SPOT Node 소켓 구성

| 구성 | 소켓 | 역할 | 비고 |
|------|------|------|------|
| 클러스터 메시 | ROUTER | Node 간 제어/데이터 라우팅 | bind+connect |
| Registry 제어 | DEALER | register/heartbeat 전송 | Registry ROUTER에 connect |
| Discovery 수신 | SUB | Registry PUB에서 peer 목록 수신 | Discovery 컴포넌트 내부 |
| 로컬 IPC | inproc 채널 | SPOT Instance ↔ Node 전달 | 네트워크 소켓 없음 |

> 로컬 IPC는 구현 세부이며, inproc 소켓 또는 내부 큐로 대체 가능하다.

---

## 3. 토픽 모델

### 3.1 토픽 소유권과 유일성

- 토픽은 **클러스터 전역에서 유일**해야 한다.
- 토픽 소유자는 **SPOT Instance**이며, 노드는 이를 중계한다.
- 이미 소유자가 존재하는 토픽을 `create`하면 **EEXIST**로 실패한다.
- **spot_id는 외부에 노출하지 않으며**, `topic_id -> spot` 매핑은 Node 내부에서 관리한다.

> 충돌(동일 토픽 동시 생성)은 지원하지 않는다.
> 애플리케이션이 토픽 유일성을 보장해야 한다.

### 3.2 토픽 라이프사이클

```
1) create (OWNER 생성)
   - SPOT -> Node: create
   - Node: OWNED 등록
   - Node -> 모든 peer: TOPIC_ANNOUNCE

2) subscribe (구독)
   - SPOT -> Node: subscribe
   - Node: 로컬 구독 등록
   - owner가 있으면 SUBSCRIBE 전송

3) publish
   - OWNED: Node가 로컬+원격에 fan-out
   - ROUTED: Node가 owner Node로 전달

4) destroy
   - SPOT -> Node: destroy
   - Node: OWNED 제거
   - Node -> 모든 peer: TOPIC_REMOVE
```

### 3.3 구독 상태 (Pending/Active)

- **Pending**: owner 미존재. 구독은 성공 처리되며, owner 발견 시 자동 활성화.
- **Active**: owner가 존재하며 SUBSCRIBE 전송 완료.

정책:
- `subscribe`는 **owner 없더라도 성공**한다.
- `publish`는 **owner 없으면 ENOENT**.

상태 전이:
```
PENDING --(owner 발견)--> ACTIVE
ACTIVE  --(owner down/제거)--> PENDING
PENDING --(unsubscribe)--> NONE
ACTIVE  --(unsubscribe)--> NONE
```

owner 발견:
- `QUERY_RESP` 수신 또는 `TOPIC_ANNOUNCE` 수신

owner down/제거:
- peer disconnect 또는 `TOPIC_REMOVE` 수신

### 3.4 토픽 명명 규칙

계층적 네임스페이스를 권장한다:
```
<domain>:<entity>:<action>
```

예시:
- `chat:room1:message`
- `zone:12:state`
- `metrics:server01:cpu`

### 3.5 패턴 구독 규칙

- 와일드카드 `*`를 지원한다.
- **단일 `*`만 허용**하며 **문자열 끝에만 위치**할 수 있다. (접두어 매칭)
- 대소문자 구분, 정규식/중간 와일드카드는 지원하지 않는다.

예시:
```
패턴: "zone:12:*"
  ├─► 매칭: "zone:12:state", "zone:12:events"
  └─► 불매칭: "zone:13:state", "zone:12"
```

### 3.6 토픽 충돌 처리 (동시 생성)

동일 토픽의 동시 생성은 **지원하지 않으며**, 애플리케이션이 예방해야 한다.
다만 분산 환경에서 경합이 발생할 수 있으므로 **결정 규칙**을 명시한다.

- 충돌 감지:
  - 이미 OWNED/ROUTED로 등록된 토픽에 대해
    다른 노드로부터 `TOPIC_ANNOUNCE`/`QUERY_RESP`가 도착하면 충돌로 간주한다.
- 소유자 결정 규칙:
  - **낮은 routing_id가 우선** (uint32 오름차순)
  - 동일 routing_id는 불가능하다고 가정한다.
- 충돌 해결:
  - 우선순위에서 **지는 노드는 OWNED → ROUTED로 강등**
  - 강등된 토픽은 publish 시 owner 노드로 라우팅된다
  - 별도의 `TOPIC_REMOVE`는 전송하지 않는다

> 토픽 유일성 보장은 여전히 애플리케이션 책임이다.

---

## 4. 노드 동기화/라우팅

### 4.1 Peer 연결/초기 동기화

- Node는 Discovery가 제공하는 **peer 목록**을 기준으로
  ROUTER connect/disconnect를 자동 수행한다.
- 연결 직후 **QUERY → QUERY_RESP**로 **OWNED 토픽 목록**을 동기화한다.
- 수동 peer connect API는 제공하지 않는다.

Discovery 연동 순서:
1) `zmq_discovery_connect_registry()`로 Registry PUB 구독
2) `zmq_discovery_subscribe(service_name)`
3) `zmq_spot_node_set_discovery(node, discovery, service_name)`

### 4.2 토픽 변경 전파

- 토픽 생성/삭제 시 **TOPIC_ANNOUNCE/TOPIC_REMOVE**를
  peer 목록 전체에 fan-out

### 4.3 구독 전파 (refcount 기반)

- 동일 Node 내 여러 SPOT이 같은 토픽을 구독할 수 있다.
- Node는 **refcount**를 유지하고, refcount가 0→1일 때만 SUBSCRIBE 전송한다.
- refcount가 1→0이 되면 UNSUBSCRIBE 전송한다.
- 패턴 구독이 존재하면, TOPIC_ANNOUNCE 수신 시 패턴 매칭 후 자동 SUBSCRIBE를 수행한다.

### 4.4 Registry HA / Failover

- Node는 Registry endpoint 목록을 보유하고 **단일 Registry에만 active 등록**한다.
- 장애 감지 시 다른 Registry로 **failover 재등록**한다.
- 상세 정책은 [04-service-discovery.md](04-service-discovery.md)의 Provider failover 규칙을 따른다.

---

## 5. 메시지 프로토콜

### 5.1 프레임 포맷

```
┌──────────────────────────────────────────┐
│ Frame 0: [routing_id] (ROUTER 자동 추가) │
│          (5B [0x00][uint32] 포맷)        │
├──────────────────────────────────────────┤
│ Frame 1: command (uint8_t)               │
├──────────────────────────────────────────┤
│ Frame 2~N: Payload                        │
└──────────────────────────────────────────┘
```

> **routing_id 포맷**은 [00-routing-id-unification.md](00-routing-id-unification.md) 스펙을 따른다.

### 5.2 Command 코드

| 코드 | 이름 | 방향 | 설명 |
|------|------|------|------|
| 0x01 | PUBLISH | Node → Owner Node | ROUTED 토픽 publish 전달 |
| 0x02 | SUBSCRIBE | Node → Owner Node | 구독 등록 |
| 0x03 | UNSUBSCRIBE | Node → Owner Node | 구독 해제 |
| 0x04 | QUERY | Node ↔ Node | OWNED 토픽 목록 요청 |
| 0x05 | QUERY_RESP | Owner Node → Node | OWNED 토픽 목록 응답 |
| 0x06 | TOPIC_ANNOUNCE | Owner Node → Node | 새 토픽 알림 |
| 0x07 | TOPIC_REMOVE | Owner Node → Node | 토픽 삭제 알림 |
| 0x08 | MESSAGE | Owner Node → Node | 구독 메시지 전달 |

### 5.3 메시지 정의

**PUBLISH**
```
Frame 1: 0x01
Frame 2: topic_id (string)
Frame 3: payload (bytes)
```

**SUBSCRIBE/UNSUBSCRIBE**
```
Frame 1: 0x02 or 0x03
Frame 2: topic_id (string)
```

**QUERY_RESP**
```
Frame 1: 0x05
Frame 2: topic_count (uint32_t)
Frame 3~N: topic_id (string, 반복)
```

**TOPIC_ANNOUNCE / TOPIC_REMOVE**
```
Frame 1: 0x06 or 0x07
Frame 2: topic_id (string)
```

**MESSAGE**
```
Frame 1: 0x08
Frame 2: topic_id (string)
Frame 3: payload (bytes)
```

---

## 6. C API 명세

### 6.1 SPOT Node (Agent)

```c
/* SPOT Node 생성/종료 */
ZMQ_EXPORT void *zmq_spot_node_new(void *ctx);
ZMQ_EXPORT int zmq_spot_node_destroy(void **node_p);

/* 클러스터 통신 바인드 */
ZMQ_EXPORT int zmq_spot_node_bind(
    void *node,
    const char *endpoint           // "tcp://*:9000"
);

/* Registry ROUTER 연결 (노드 등록/Heartbeat용) */
ZMQ_EXPORT int zmq_spot_node_connect_registry(
    void *node,
    const char *registry_router_endpoint
);

/* SPOT 노드 등록 */
ZMQ_EXPORT int zmq_spot_node_register(
    void *node,
    const char *service_name,      // NULL이면 "spot-node"
    const char *advertise_endpoint // NULL이면 bind 주소에서 자동 감지
);

/* SPOT 노드 등록 해제 */
ZMQ_EXPORT int zmq_spot_node_unregister(
    void *node,
    const char *service_name       // NULL이면 "spot-node"
);

/* Discovery 연동 (peer 자동 연결/해제) */
ZMQ_EXPORT int zmq_spot_node_set_discovery(
    void *node,
    void *discovery,
    const char *service_name       // NULL이면 "spot-node"
);
```

- `zmq_spot_node_connect_registry()`는 **여러 번 호출 가능**하며,
  Registry endpoint 목록을 구성한다.
- Node는 목록 중 **하나에만 active 등록/Heartbeat**를 전송한다.
- Discovery 미설정 시 Node는 **단일 노드(LOCAL) 모드**로 동작한다.

**수명 규칙**:
- Node는 **SPOT Instance보다 먼저 생성**되어야 한다.
- Node는 **연결된 모든 SPOT Instance보다 늦게 종료**되어야 한다.

**서비스명 규칙** (노드 단위):
- 기본값 `spot-node`
- 클러스터 분리를 위해 `spot-node.<cluster-id>` 사용 가능
- 허용 문자: `[a-z0-9.-]`, 최대 64자
- `spot-node`는 SPOT 전용 **예약 이름**으로 간주한다.
- 규칙 위반 시 `EINVAL` 반환

**연결 정책**:
- peer 연결은 **Discovery 기반 자동 연결/해제**만 지원한다.
- 별도의 수동 peer connect API는 제공하지 않는다.

### 6.2 SPOT Instance

```c
/* SPOT 인스턴스 생성/종료 */
ZMQ_EXPORT void *zmq_spot_new(void *node);
ZMQ_EXPORT void *zmq_spot_new_threadsafe(void *node);
ZMQ_EXPORT int zmq_spot_destroy(void **spot_p);

/* 토픽 생성/삭제 (OWNED) */
ZMQ_EXPORT int zmq_spot_topic_create(
    void *spot,
    const char *topic_id
);

ZMQ_EXPORT int zmq_spot_topic_destroy(
    void *spot,
    const char *topic_id
);

/* 메시지 발행 */
ZMQ_EXPORT int zmq_spot_publish(
    void *spot,
    const char *topic_id,
    zmq_msg_t *msg,
    int flags
);

/* 구독/해제 */
ZMQ_EXPORT int zmq_spot_subscribe(
    void *spot,
    const char *topic_id
);

ZMQ_EXPORT int zmq_spot_subscribe_pattern(
    void *spot,
    const char *pattern
);

ZMQ_EXPORT int zmq_spot_unsubscribe(
    void *spot,
    const char *topic_id_or_pattern
);

/* 메시지 수신 */
ZMQ_EXPORT int zmq_spot_recv(
    void *spot,
    zmq_msg_t *msg,
    int flags,
    char *topic_id_out,            // 토픽명 반환 (256B 버퍼, NULL 가능)
    size_t *topic_id_len           // 토픽명 길이 반환 (NULL 가능)
);
```

**에러 정책**
- `zmq_spot_topic_create`: 이미 owner 존재 시 `EEXIST`
- `zmq_spot_publish`: owner 없으면 `ENOENT`
- `zmq_spot_subscribe`: owner 없어도 **성공(대기)**
- `zmq_spot_unsubscribe`: exact 또는 pattern 문자열 모두 허용

---

## 7. 사용 예시

### 7.1 단일 서버, 다중 SPOT

```c
void *ctx = zmq_ctx_new();
void *node = zmq_spot_node_new(ctx);

void *spot1 = zmq_spot_new(node);
void *spot2 = zmq_spot_new(node);

zmq_spot_topic_create(spot1, "chat:room1");
zmq_spot_subscribe(spot2, "chat:room1");

zmq_msg_t msg;
zmq_msg_init_size(&msg, 5);
memcpy(zmq_msg_data(&msg), "hello", 5);
zmq_spot_publish(spot1, "chat:room1", &msg, 0);
```

### 7.2 두 서버, 다중 SPOT

```c
// Server A
void *ctxA = zmq_ctx_new();
void *nodeA = zmq_spot_node_new(ctxA);
void *discoveryA = zmq_discovery_new(ctxA);
zmq_discovery_connect_registry(discoveryA, "tcp://registry1:5550");
zmq_discovery_subscribe(discoveryA, "spot-node");

zmq_spot_node_bind(nodeA, "tcp://*:9000");
zmq_spot_node_connect_registry(nodeA, "tcp://registry1:5551");
zmq_spot_node_register(nodeA, "spot-node", NULL);
zmq_spot_node_set_discovery(nodeA, discoveryA, "spot-node");

void *spotA1 = zmq_spot_new(nodeA);
zmq_spot_topic_create(spotA1, "zone:12:state");

// Server B
void *ctxB = zmq_ctx_new();
void *nodeB = zmq_spot_node_new(ctxB);
void *discoveryB = zmq_discovery_new(ctxB);
zmq_discovery_connect_registry(discoveryB, "tcp://registry1:5550");
zmq_discovery_subscribe(discoveryB, "spot-node");

zmq_spot_node_bind(nodeB, "tcp://*:9001");
zmq_spot_node_connect_registry(nodeB, "tcp://registry1:5551");
zmq_spot_node_register(nodeB, "spot-node", NULL);
zmq_spot_node_set_discovery(nodeB, discoveryB, "spot-node");

void *spotB1 = zmq_spot_new(nodeB);
zmq_spot_subscribe(spotB1, "zone:12:state");
```

### 7.3 MMORPG 존 패턴

- 토픽은 **존 단위로 고정**한다.
- 플레이어/오브젝트는 payload에서 식별한다.

```
zone:12:state
zone:12:events
zone:13:state
zone:13:events
```

인접 존 공유:
- `zone:12:*`를 구독하는 노드는 `zone:13:*`도 구독하여 경계 데이터를 공유
- 토픽 생성/삭제는 존 생성/폐기 시에만 발생

```c
zmq_spot_subscribe_pattern(spot, "zone:12:*");
zmq_spot_subscribe_pattern(spot, "zone:13:*");
```

---

## 8. 구현 계획

1) SPOT Node 내부 자료구조
   - topic_owner_map (topic_id -> local spot or remote node)
   - local_sub_map (topic_id -> spot list + refcount)
   - remote_sub_map (topic_id -> subscriber node list)

2) Node 간 ROUTER/ROUTER 메시지 처리
   - QUERY/QUERY_RESP
   - TOPIC_ANNOUNCE/REMOVE
   - SUBSCRIBE/UNSUBSCRIBE
   - PUBLISH/MESSAGE

3) SPOT Instance API 구현
   - create/destroy
   - publish/subscribe/recv

4) Discovery/Registry 연동
   - 노드 목록 갱신
   - 자동 connect/disconnect

---

## 9. 검증 방법

### 9.1 단위 테스트

- `test_spot_topic_unique`: 동일 토픽 생성 시 EEXIST
- `test_spot_pending_subscribe`: owner 없을 때 subscribe 성공 + pending 상태
- `test_spot_publish_no_owner`: owner 없을 때 publish ENOENT
- `test_spot_local_fanout`: 동일 Node 내 다중 SPOT 구독자 전달
- `test_spot_remote_fanout`: 원격 노드 구독자에게 전달
- `test_spot_refcount_subscribe`: refcount 0→1, 1→0에만 SUB/UNSUB 전송
- `test_spot_pattern_subscribe`: 패턴 구독 매칭 시 자동 SUBSCRIBE
- `test_spot_owner_conflict`: 동일 토픽 동시 생성 시 우선순위 규칙 적용
- `test_spot_pending_reactivate`: owner down 후 재등장 시 pending → active 복구

### 9.2 통합 테스트

시나리오 1: 단일 서버 다중 SPOT
1. Node 1개 생성
2. SPOT 10개 생성
3. 1개 SPOT이 토픽 생성
4. 나머지 SPOT이 구독
5. publish → 모두 수신 확인

시나리오 2: 2노드 클러스터
1. Registry/Discovery 구성
2. Node A/B 등록
3. A가 토픽 생성
4. B가 구독
5. A publish → B 수신 확인

시나리오 3: owner down/up
1. A가 토픽 생성
2. B가 구독
3. A 종료 → B는 pending 전환
4. A 재시작 후 토픽 생성 → B 자동 활성화

---

## 10. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 2.3 | 2026-01-27 | SPOT Node 소켓 구성 명시 |
| 2.2 | 2026-01-27 | Discovery 기반 peer 연결/해제 명시 및 예시 반영 |
| 2.1 | 2026-01-27 | 구독 상태 전이/패턴 규칙/충돌 처리/수명 규칙/테스트 보강 |
| 2.0 | 2026-01-27 | SPOT Instance/Node 분리 모델로 전면 재작성, OWNED/ROUTED 정의, Pending 구독 정책 명시 |
