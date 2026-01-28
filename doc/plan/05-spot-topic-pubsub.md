# SPOT 토픽 PUB/SUB 스펙 (SPOT Topic PUB/SUB)

> **우선순위**: 5 (Core Feature)
> **상태**: Draft
> **버전**: 3.1
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
- **다중 인스턴스**: 서버/프로세스당 SPOT 인스턴스 **수천~만(10k) 단위** 생성 가능
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
│    └─────── local IPC/queue ┴───────┐      │
│                                     ▼      │
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
- SPOT Instance ↔ Node 통신은 **내부 큐/IPC 채널**로 처리한다.
- 네트워크 통신/Discovery/라우팅은 **SPOT Node**가 담당한다.

### 2.2 클러스터 구조 (No SPoF, Router-only Mesh)

```
Node A (ROUTER)  <──►  Node B (ROUTER)  <──►  Node C (ROUTER)
   ▲  ▲                          ▲                    ▲
   │  └── local IPC/queue ─ SPOTs └── local IPC/queue ─ SPOTs
```

- 모든 노드는 **ROUTER 소켓 하나로 bind+connect**를 수행한다.
- **단일 허브 의존 구조는 금지**한다.
- **노드 간 연결은 Discovery 기반 자동 연결/해제**만 지원한다.

### 2.3 노드 ID와 routing_id

- 각 Node는 생성 시 **고유 node_id(uint32)**를 가진다.
- Node는 ROUTER 소켓의 **identity(routing_id)를 node_id로 설정**한다.
- Registry에는 **endpoint + node_id**가 함께 등록된다.
  - Discovery가 제공하는 peer 목록에는 node_id가 포함된다.
- Node 간 라우팅은 **node_id를 routing_id로 사용**한다.

### 2.4 데이터 흐름 (SPOT Instance 관점)

**OWNED 토픽 publish**
```
SPOT#1 (owner) -> Node -> [로컬 구독자] + [원격 구독 노드들]
```

**ROUTED 토픽 publish**
```
SPOT#2 -> Node -> (owner Node로 PUBLISH) -> owner Node -> 구독자들에게 fan-out
```

### 2.5 전파 방식 명시

- 토픽 생성/삭제 알림은 **모든 peer 노드에 fan-out**된다.
- 브로드캐스트/멀티캐스트 기능은 제공하지 않는다.
- 구현은 **peer 목록을 순회하며 routing_id를 바꿔 N회 전송**하는 방식이다.

### 2.6 Thread-safety

- **SPOT Instance는 기본적으로 thread-safe 하지 않다.**
  - 단일 스레드에서 사용 권장
  - 필요 시 `zmq_spot_new_threadsafe()` 사용
- **SPOT Node는 내부 동기화를 포함하며 thread-safe**하게 제공한다.
  - 다수의 SPOT Instance가 동시에 접근 가능

### 2.7 SPOT Node 소켓 구성

| 구성 | 소켓 | 역할 | 비고 |
|------|------|------|------|
| 클러스터 메시 | ROUTER | Node 간 제어/데이터 라우팅 | bind+connect |
| Registry 제어 | DEALER | register/heartbeat 전송 | Registry ROUTER에 connect |
| Discovery 수신 | SUB | Registry PUB에서 peer 목록 수신 | Discovery 컴포넌트 내부 |
| 로컬 IPC | 내부 큐/채널 | SPOT Instance ↔ Node 전달 | 네트워크 소켓 없음 |

> 로컬 IPC는 **ZeroMQ inproc 전송이 아니라 내부 큐 기반**을 기본으로 한다.
> 필요 시 inproc 소켓으로 대체할 수 있으나, 대규모 인스턴스(10k) 기준으로는
> 내부 큐 방식이 더 효율적이다.

### 2.8 내부 처리 모델 (구현 수준)

- SPOT Node는 **단일 직렬 처리 루프**를 통해
  네트워크 수신/송신, 토픽/구독 업데이트를 처리한다.
- thread-safe API 호출은 **Node 내부 큐**에 적재되어
  루프에서 순차적으로 처리된다.
- 목적:
  - 토픽/구독 맵의 일관성 유지
  - publish/subscribe 순서 보장
  - 멀티스레드 호출 시 데이터 레이스 방지

### 2.9 참고: ZLink PUB/SUB 필터링 동작

ZLink는 **ZeroMQ v3.x 기반** 동작을 따른다. 따라서 필터링 기준은 아래와 같다.

- **TCP/IPC (connected transport)**: 필터링은 **PUB(또는 XPUB) 측**에서 수행된다.
- **PGM/EPGM 멀티캐스트**: 필터링은 **SUB 측**에서 수행된다.
  - 현재 ZLink에는 미지원이며, **추후 추가 예정**이다.
- **XPUB/XSUB**: 구독/해제 메시지를 애플리케이션이 직접 수신/발행 가능하다.

이 차이는 네트워크 효율성과 설계 선택(팬아웃 비용, 구독 가시성)에 영향을 준다.

### 2.10 API 호출 순서 (권장 흐름)

#### Node

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_spot_node_new()` | Node 생성 (node_id 생성) |
| 2 | `zmq_spot_node_bind()` | 클러스터 ROUTER bind |
| 3 | `zmq_spot_node_connect_registry()` | Registry ROUTER 연결 |
| 4 | `zmq_spot_node_register()` | 서비스 등록 + Heartbeat 시작 |
| 5 | `zmq_discovery_new()` | Discovery 생성 |
| 6 | `zmq_discovery_connect_registry()` | Registry PUB 구독 |
| 7 | `zmq_discovery_subscribe()` | service_name 구독 |
| 8 | `zmq_spot_node_set_discovery()` | peer 자동 연결 시작 |

#### SPOT Instance

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_spot_new()` | SPOT 생성 |
| 2 | `zmq_spot_topic_create()` | OWNED 토픽 생성 |
| 3 | `zmq_spot_subscribe()` | 토픽 구독 |
| 4 | `zmq_spot_publish()` | 메시지 발행 |
| 5 | `zmq_spot_recv()` | 메시지 수신 |

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

### 3.3 토픽 전달 모드 (Delivery Mode)

기본 동작은 **SPOT별 큐 방식**이며, 필요 시 토픽 단위로 **RingBuffer 모드**를
명시적으로 선택할 수 있다.

| 모드 | 설명 | 적합한 경우 |
|------|------|------------|
| **QUEUE (기본)** | 구독 SPOT별 큐에 enqueue | 구독자 수가 적거나 토픽 수가 많은 경우 |
| **RINGBUFFER (옵션)** | 토픽 로그에 1회 append + 구독자는 커서로 pull | 팬아웃이 큰 토픽 |

**RINGBUFFER 모드 특징**
- publish 시 1회 append (O(1))
- 구독자 수에 비례한 enqueue 비용이 없음
- 느린 구독자는 HWM 정책에 따라 drop/skip

**모드 설정 API**
```c
/* 토픽 생성 시 모드 지정 */
ZMQ_EXPORT int zmq_spot_topic_create_ex(
    void *spot,
    const char *topic_id,
    int mode              /* ZMQ_SPOT_TOPIC_QUEUE | ZMQ_SPOT_TOPIC_RINGBUFFER */
);
```

> 기본 `zmq_spot_topic_create()`는 `QUEUE` 모드로 동작한다.

### 3.4 구독 상태 (Pending/Active)

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

### 3.5 토픽 명명 규칙

계층적 네임스페이스를 권장한다:
```
<domain>:<entity>:<action>
```

예시:
- `chat:room1:message`
- `zone:12:state`
- `metrics:server01:cpu`

### 3.6 패턴 구독 규칙

- 와일드카드 `*`를 지원한다.
- **단일 `*`만 허용**하며 **문자열 끝에만 위치**할 수 있다. (접두어 매칭)
- 대소문자 구분, 정규식/중간 와일드카드는 지원하지 않는다.

예시:
```
패턴: "zone:12:*"
  ├─► 매칭: "zone:12:state", "zone:12:events"
  └─► 불매칭: "zone:13:state", "zone:12"
```

### 3.7 토픽/패턴 검증 규칙

- `topic_id`/`pattern`은 **빈 문자열을 허용하지 않는다**.
- `topic_id`는 **최대 255 바이트**로 제한한다.
- `pattern`은 `*` 한 개만 허용하며 **문자열 끝에만 위치**해야 한다.
- 위 규칙 위반 시 `EINVAL`을 반환한다.

### 3.8 토픽 충돌 처리 (동시 생성)

동일 토픽의 동시 생성은 **지원하지 않으며**, 애플리케이션이 예방해야 한다.
다만 분산 환경에서 경합이 발생할 수 있으므로 **결정 규칙**을 명시한다.

- 충돌 감지:
  - 이미 OWNED/ROUTED로 등록된 토픽에 대해
    다른 노드로부터 `TOPIC_ANNOUNCE`/`QUERY_RESP`가 도착하면 충돌로 간주한다.
- 소유자 결정 규칙:
  - **낮은 node_id가 우선** (uint32 오름차순)
  - 동일 node_id는 불가능하다고 가정한다.
- 충돌 해결:
  - 우선순위에서 **지는 노드는 OWNED → ROUTED로 강등**
  - 이후 publish는 **owner 노드로 라우팅**된다
  - `zmq_spot_topic_create()`는 **이미 owner를 알고 있으면 EEXIST**를 반환한다
  - 동시 생성 이후 충돌이 뒤늦게 발견되는 경우에도 **강등은 발생**한다

> 토픽 유일성 보장은 여전히 애플리케이션 책임이다.

---

## 4. 노드 동기화/라우팅

### 4.1 Peer 연결/초기 동기화

- Node는 Discovery가 제공하는 **peer 목록**을 기준으로
  ROUTER connect/disconnect를 자동 수행한다.
- 연결 직후 **QUERY → QUERY_RESP**로 **OWNED 토픽 목록**을 동기화한다.
- 수동 peer connect API는 제공하지 않는다.
  - Discovery 목록에는 **endpoint + node_id(routing_id)**가 포함된다.
  - Node는 node_id를 알고 있으므로 **즉시 QUERY 전송이 가능**하다.

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
- 패턴 구독 등록 시 **이미 알고 있는 토픽 목록에도 즉시 매칭**을 수행한다.

### 4.4 연결 관리 상세 (구현 수준)

Node는 Discovery의 **peer 목록**을 기준으로 연결을 관리한다.

**Peer 테이블**
```
peer_entry:
  - endpoint
  - routing_id (수신 시 확정)
  - state: DISCONNECTED | CONNECTING | CONNECTED
```

**Discovery에 peer 추가됨**
1) endpoint == self 이면 무시
2) peer 테이블에 없으면 CONNECTING으로 등록
3) ROUTER connect 수행
4) 연결 수립 후 첫 메시지 수신 시 CONNECTED로 전환
5) QUERY 전송 → 토픽 목록 동기화

**Discovery에서 peer 제거됨 / 연결 끊김**
1) peer 상태 DISCONNECTED
2) 해당 peer가 owner인 토픽은 **owner unknown**으로 전환
   - 구독은 Pending 상태 유지
3) 해당 peer가 구독자로 등록된 토픽은 **remote_sub_map에서 제거**
4) 필요 시 재연결 시도는 Discovery 갱신에 따름

> 연결 상태는 ZMQ monitor 이벤트(`CONNECTION_READY`, `DISCONNECTED`)로 보정 가능하다.

### 4.5 Registry HA / Failover

- Node는 Registry endpoint 목록을 보유하고 **단일 Registry에만 active 등록**한다.
- 장애 감지 시 다른 Registry로 **failover 재등록**한다.
- 상세 정책은 [04-service-discovery.md](04-service-discovery.md)의 Provider failover 규칙을 따른다.
- **SPOT Node는 Provider 역할로 등록/Heartbeat**를 전송한다.
  - Registry는 Heartbeat TTL로 **노드 생존 여부를 판단**한다.
  - Discovery는 Registry PUB 목록을 그대로 사용하므로,
    목록에서 제거되면 해당 노드는 **down**으로 처리된다.

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

**QUERY**
```
Frame 1: 0x04
// 추가 payload 없음 (전체 목록 요청)
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

### 5.4 메시지 흐름 (구현 수준)

**A) 토픽 생성 (OWNED)**
```
SPOT#A -> NodeA: CREATE(topic)
NodeA: OWNED 등록
NodeA -> 모든 Peer: TOPIC_ANNOUNCE(topic)
PeerB: owner=NodeA 등록, pending 구독 매칭 시 SUBSCRIBE 전송
```

**B) 구독 (owner 존재)**
```
SPOT#B -> NodeB: SUBSCRIBE(topic)
NodeB: local_sub_map++ (refcount)
NodeB -> NodeA(owner): SUBSCRIBE(topic)
NodeA: remote_sub_map에 NodeB 추가
```

**C) 구독 (owner 미존재)**
```
SPOT#B -> NodeB: SUBSCRIBE(topic)
NodeB: local_sub_map++ (Pending)
... 이후 owner 등장 ...
NodeB -> NodeA(owner): SUBSCRIBE(topic)
```

**D) publish (OWNED)**
```
SPOT#A -> NodeA: PUBLISH(topic, msg)
NodeA -> 로컬 구독 SPOT들: MESSAGE
NodeA -> remote_sub_map의 Node들: MESSAGE
```

**E) publish (ROUTED)**
```
SPOT#B -> NodeB: PUBLISH(topic, msg)
NodeB -> NodeA(owner): PUBLISH
NodeA -> 로컬/원격 구독자: MESSAGE fan-out
```

**F) owner down**
```
NodeB: peer disconnect 감지
NodeB: owner unknown 전환, 구독은 Pending 유지
... owner 재등장 ...
NodeB: SUBSCRIBE 재전송
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
- `advertise_endpoint == NULL`인 경우:
  - bind가 1개면 해당 endpoint를 사용한다.
  - bind가 2개 이상이면 **EINVAL**을 반환한다.

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

/* 토픽 생성 (모드 지정) */
ZMQ_EXPORT int zmq_spot_topic_create_ex(
    void *spot,
    const char *topic_id,
    int mode              // ZMQ_SPOT_TOPIC_QUEUE | ZMQ_SPOT_TOPIC_RINGBUFFER
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
- `topic_id`/`pattern` 규칙 위반 시 `EINVAL`

**정리 규칙**
- `zmq_spot_destroy()`는 해당 SPOT이 보유한
  - OWNED 토픽을 **자동 destroy** 하고
  - 구독/패턴 구독을 **자동 해제**한다.
- 이후 refcount가 0이 되는 토픽은 UNSUBSCRIBE 전송 대상이 된다.

**수신 큐 (구현 수준)**
- `QUEUE` 모드:
  - 각 SPOT은 **개별 수신 큐(FIFO)**를 가진다.
  - Node는 MESSAGE를 수신하면 **해당 토픽을 구독 중인 SPOT 큐에 enqueue**한다.
  - 큐는 **HWM을 가진 고정 크기**로 두며, 초과 시 **해당 SPOT에만 드롭**한다.
  - publish는 큐 드롭과 무관하게 성공/실패를 결정한다.
- `RINGBUFFER` 모드:
  - 토픽 로그에 1회 append
  - SPOT은 **커서 기반 pull**로 메시지를 읽는다
  - 느린 구독자는 HWM 초과 시 drop/skip 처리

### 6.3 내부 제어 채널 (개념)

SPOT Instance와 Node 간에는 내부 제어 채널이 존재한다.
외부 API는 아래 메시지로 변환되어 Node로 전달된다.

| API | 내부 명령 | 설명 |
|-----|----------|------|
| `zmq_spot_topic_create` | CREATE | OWNED 토픽 생성 요청 |
| `zmq_spot_topic_destroy` | DESTROY | OWNED 토픽 제거 요청 |
| `zmq_spot_subscribe` | SUBSCRIBE | 구독 등록 |
| `zmq_spot_subscribe_pattern` | SUBSCRIBE_PATTERN | 패턴 구독 등록 |
| `zmq_spot_unsubscribe` | UNSUBSCRIBE | 구독 해제 |
| `zmq_spot_publish` | PUBLISH | 메시지 발행 요청 |
| `zmq_spot_recv` | RECV | 수신 대기/가져오기 |

> 내부 명령은 구현 상세이며 ABI에 노출하지 않는다.

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
   - topic_owner_map (topic_id -> owner node + owner spot)
   - local_topic_map (topic_id -> owning spot)
   - local_sub_map (topic_id -> spot list + refcount + pending flag)
   - remote_sub_map (topic_id -> subscriber node list)
   - peer_table (endpoint -> routing_id + state)

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
   - failover 재등록

5) 로컬 multiplex 처리
   - 다수 SPOT의 구독 refcount 관리
   - publish/recv 경로에서 spot 라우팅

6) 대규모 SPOT 인스턴스 지원 (기본 구현 요구사항)
   - SPOT Instance는 **소켓/스레드/타이머를 직접 소유하지 않는다**
   - 인스턴스는 **경량 핸들(spot_id + node 포인터)**로 유지한다
   - 구독/토픽/peer 맵은 **Node 단위로만 보유**한다
   - local_sub_map은 **spot_id 목록(압축/벡터)**으로 관리하고,
     fan-out 시 **복사 최소화**한다
   - 패턴 구독은 **접두어 인덱스**로 후보를 줄인다
   - 수신 큐는 **lazy 생성 + HWM 적용** (수신 폭주 방지)
   - per-spot 메모리 사용량은 **고정/소량**을 목표로 한다

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
- `test_spot_peer_connect_flow`: Discovery 추가 → connect → QUERY 동기화
- `test_spot_peer_remove_cleanup`: peer 제거 시 owner unknown/pending 처리
- `test_spot_scale_instances`: 단일 Node에서 SPOT 10k 생성/구독/해제 기본 동작

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
| 3.1 | 2026-01-27 | 토픽 전달 모드(QUEUE/RINGBUFFER) 및 API 추가 |
| 3.0 | 2026-01-27 | 다중 인스턴스 목표(10k) 명확화 |
| 2.9 | 2026-01-27 | 노드 ID/라우팅, API 순서, QUERY 포맷, 큐/정리 규칙 등 상세 보강 |
| 2.8 | 2026-01-27 | SPOT Node 생존 판단(Provider/Heartbeat) 명시 |
| 2.7 | 2026-01-27 | 대규모 SPOT 인스턴스 지원 요구사항/테스트 추가 |
| 2.6 | 2026-01-27 | ZLink 기준 PUB/SUB 필터링 동작으로 정리 |
| 2.5 | 2026-01-27 | ZeroMQ PUB/SUB 필터링 동작 참고 섹션 추가 |
| 2.4 | 2026-01-27 | 메시지 흐름/연결 관리/내부 채널/구현 수준 상세 추가 |
| 2.3 | 2026-01-27 | SPOT Node 소켓 구성 명시 |
| 2.2 | 2026-01-27 | Discovery 기반 peer 연결/해제 명시 및 예시 반영 |
| 2.1 | 2026-01-27 | 구독 상태 전이/패턴 규칙/충돌 처리/수명 규칙/테스트 보강 |
| 2.0 | 2026-01-27 | SPOT Instance/Node 분리 모델로 전면 재작성, OWNED/ROUTED 정의, Pending 구독 정책 명시 |
