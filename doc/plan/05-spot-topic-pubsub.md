# SPOT 토픽 PUB/SUB 스펙 (SPOT Topic PUB/SUB)

> **우선순위**: 5 (Core Feature)
> **상태**: Draft
> **버전**: 4.1
> **의존성**:
> - [00-routing-id-unification.md](00-routing-id-unification.md) (node_id 포맷)
> - [04-service-discovery.md](04-service-discovery.md) (Registry/Discovery 연동)

## 목차
1. [개요](#1-개요)
2. [아키텍처](#2-아키텍처)
3. [토픽 모델](#3-토픽-모델)
4. [구독 동기화/전달](#4-구독-동기화전달)
5. [메시지 포맷](#5-메시지-포맷)
6. [C API 명세](#6-c-api-명세)
7. [사용 예시](#7-사용-예시)
8. [구현 계획](#8-구현-계획)
9. [검증 방법](#9-검증-방법)
10. [변경 이력](#10-변경-이력)

---

## 1. 개요

### 1.1 배경

기존 ZMQ PUB/SUB는 엔드포인트를 직접 지정해야 하므로 위치 투명성이 없다.
SPOT은 **토픽 이름만으로 발행/구독**할 수 있는 추상화 계층을 제공한다.

### 1.2 목표

- **위치 투명성**: 토픽 이름만으로 발행/구독
- **다중 인스턴스**: 서버/프로세스당 SPOT 인스턴스 **수천~만(10k) 단위** 생성 가능
- **No SPoF**: 단일 허브 의존 구조 금지
- **단순 데이터 플레인**: PUB/SUB 기반으로 최대한 단순화
- **확장성**: 노드 수 증가에도 예측 가능한 비용

### 1.3 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Instance** | 애플리케이션이 생성하는 경량 핸들. publish/subscribe 호출 주체. |
| **SPOT Node(Agent)** | 서버/프로세스 당 1개 권장. 클러스터 통신/Discovery/전달 담당. |
| **Topic** | 메시지를 분류하는 문자열 키 (prefix match) |
| **Pattern** | 접두어 매칭 패턴. `*`는 끝에 1개만 허용 |
| **Subscription** | 토픽/패턴에 대한 구독 등록 |

### 1.4 설계 요약

- 데이터 플레인은 **PUB/SUB만 사용**한다.
- 노드 간 제어-plane 메시지(QUERY/ANNOUNCE 등)는 없다.
- Discovery는 **노드 연결 관리**에만 사용한다.

---

## 2. 아키텍처

### 2.1 단일 서버 구조 (다중 SPOT 인스턴스)

```
┌────────────────────────────────────────────┐
│                  Server                    │
│                                            │
│  SPOT#1   SPOT#2   ...   SPOT#N             │
│    │        │              │               │
│    └─────── local queue (default) ──┴──┐   │
│                                     ▼      │
│                        ┌────────────────┐  │
│                        │  SPOT Node     │  │
│                        │  (Agent)       │  │
│                        │  - Sub Map     │  │
│                        │  - PUB/SUB     │  │
│                        └────────────────┘  │
└────────────────────────────────────────────┘
```

- SPOT Instance는 네트워크 소켓을 직접 갖지 않는다.
- SPOT Instance ↔ Node 전달은 **내부 큐(기본)**로 처리하며,
  토픽별로 **RINGBUFFER 모드**를 선택할 수 있다.

### 2.2 클러스터 구조 (PUB/SUB Mesh)

```
Node A (PUB+SUB)  <── connect ──  Node B (PUB+SUB)  <── connect ──  Node C (PUB+SUB)
   ▲  ▲                               ▲  ▲                              ▲  ▲
   │  └─ local queue ─ SPOTs           │  └─ local queue ─ SPOTs          │  └─ local queue ─ SPOTs
```

- 각 Node는 **PUB를 bind**하고, **SUB를 peer PUB에 connect**한다.
- 허브/중계 노드는 **필수가 아니며**, 기본은 mesh 구성이다.
- **노드 간 연결은 Discovery 기반 자동 연결/해제**만 지원한다.

### 2.3 Node ID와 Discovery 연동

- Node는 생성 시 **고유 node_id(uint32)**를 가진다.
- Registry에는 **pub endpoint + node_id**가 함께 등록된다.
- Discovery는 **peer 목록(노드 + pub endpoint)**을 제공한다.
- Node는 Discovery 목록을 기준으로 **SUB connect/disconnect**를 수행한다.
- 자신의 node_id는 **자기 자신 연결을 회피**하는 데만 사용한다.

> PUB/SUB 데이터 플레인에서는 routing_id를 사용하지 않는다.

### 2.4 데이터 흐름

**로컬 publish**
```
SPOT#1 -> Node -> [로컬 구독자] + [PUB로 원격 노드 전송]
```

**원격 publish 수신**
```
Peer PUB -> Node SUB -> [로컬 구독자]
```

정책:
- 원격에서 받은 메시지는 **재발행하지 않는다**.
- 동일 토픽은 **여러 Node/Spot에서 동시에 발행 가능**하다.
- 자기 자신 publish에 대한 로컬 수신은 **구독 여부에 따라 동작**한다.

### 2.5 연결 관리 상세

- Discovery에 새로운 peer가 등장하면 **SUB connect**를 수행한다.
- 이미 설정된 SUB 필터는 연결 직후 자동 전파된다.
- peer 제거 시 **disconnect**를 수행한다.
- 재연결은 Discovery 재등장 이벤트로 처리한다.

### 2.6 Thread-safety

- **SPOT Instance는 기본적으로 thread-safe 하지 않다.**
  - 단일 스레드에서 사용 권장
  - 필요 시 `zmq_spot_new_threadsafe()` 사용
- **SPOT Node는 내부 동기화를 포함하며 thread-safe**하게 제공한다.

### 2.7 SPOT Node 소켓 구성

| 구성 | 소켓 | 역할 | 비고 |
|------|------|------|------|
| 데이터 플레인 송신 | PUB | 로컬 publish를 원격 노드로 전송 | bind |
| 데이터 플레인 수신 | SUB | 원격 publish 수신 | peer PUB에 connect |
| Registry 제어 | DEALER | register/heartbeat 전송 | Registry ROUTER에 connect |
| Discovery 수신 | SUB | Registry PUB에서 peer 목록 수신 | Discovery 컴포넌트 내부 |
| 로컬 큐 | 내부 큐 | SPOT Instance ↔ Node 전달 | 네트워크 소켓 없음 |

> 로컬 전달은 **기본적으로 per-spot 큐(QUEUE)** 방식이다.
> 특정 토픽에 한해 **RINGBUFFER 모드**를 선택할 수 있다.

### 2.8 내부 처리 모델 (구현 수준)

- SPOT Node는 **단일 직렬 처리 루프**를 통해
  네트워크 수신/송신, 토픽/구독 업데이트를 처리한다.
- thread-safe API 호출은 **Node 내부 큐**에 적재되어
  루프에서 순차적으로 처리된다.
- 목적:
  - 구독 맵 일관성 유지
  - publish/subscribe 순서 보장
  - 멀티스레드 호출 시 데이터 레이스 방지

### 2.9 참고: ZLink PUB/SUB 필터링 동작

ZLink는 **ZeroMQ v3.x 기반** 동작을 따른다. 따라서 필터링 기준은 아래와 같다.

- **TCP/IPC (connected transport)**: 필터링은 **PUB(또는 XPUB) 측**에서 수행된다.
- **PGM/EPGM 멀티캐스트**: 필터링은 **SUB 측**에서 수행된다.
  - 현재 ZLink에는 미지원이며, **추후 추가 예정**이다.
- **XPUB/XSUB**: 구독/해제 메시지를 애플리케이션이 직접 수신/발행 가능하다.

### 2.10 API 호출 순서 (권장 흐름)

#### Node

| 단계 | 호출 | 설명 |
|------|------|------|
| 1 | `zmq_spot_node_new()` | Node 생성 (node_id 생성) |
| 2 | `zmq_spot_node_bind()` | PUB bind (클러스터 송신 endpoint) |
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
| 2 | `zmq_spot_topic_create()` | (선택) 토픽 설정/모드 지정 |
| 3 | `zmq_spot_subscribe()` | 토픽 구독 |
| 4 | `zmq_spot_publish()` | 메시지 발행 |
| 5 | `zmq_spot_recv()` | 메시지 수신 |

---

## 3. 토픽 모델

### 3.1 토픽 의미

- 토픽은 **클러스터 전역 문자열 키**이다.
- 토픽 **소유자 개념은 없다**. (다중 발행 허용)
- `topic_create`는 **로컬 설정을 위한 선택적 API**이다.
  - 네트워크에 토픽을 “등록”하지 않는다.

### 3.2 토픽 라이프사이클 (로컬)

```
1) create (선택)
   - SPOT -> Node: create
   - Node: 로컬 토픽 설정/모드 등록

2) subscribe
   - SPOT -> Node: subscribe
   - Node: 로컬 구독 등록
   - Node: SUB 필터 업데이트

3) publish
   - Node: 로컬 구독자 전달 + PUB 전송

4) destroy (선택)
   - SPOT -> Node: destroy
   - Node: 로컬 토픽 설정 제거
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

### 3.4 구독 모델

- `subscribe`는 **즉시 활성화**된다.
- “owner 미존재” 개념이 없으므로 **Pending 상태는 없다**.
- `publish`는 구독자가 없어도 **성공**한다.

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

### 3.8 다중 발행 정책

- 동일 토픽에 대해 **여러 Node/Spot이 동시에 발행 가능**하다.
- 토픽 충돌/소유권 개념이 없으므로 **EEXIST/ENOENT 정책은 적용하지 않는다**.

---

## 4. 구독 동기화/전달

### 4.1 Node 내 구독 집계 (refcount)

- Node는 **로컬 SPOT들의 구독 refcount**를 유지한다.
- refcount가 0→1일 때만 **SUBSCRIBE**를 수행한다.
- refcount가 1→0이 되면 **UNSUBSCRIBE**를 수행한다.
- 패턴 구독은 **prefix 필터**로 SUB에 등록한다.
  - 패턴 `zone:12:*` → SUB 필터 `"zone:12:"`

### 4.2 로컬 전달 (Node → SPOT)

- Node는 수신한 메시지를 **로컬 구독 맵**으로 매칭한다.
- 매칭된 SPOT 큐에 **enqueue**하거나, RINGBUFFER 모드라면 **append**한다.
- 동일 토픽을 구독한 SPOT 수에 비례한 비용이 발생한다.
  - RINGBUFFER 모드는 이 비용을 완화한다.

### 4.3 원격 전달 (Node → PUB)

- 로컬 publish는 항상 **PUB로 전송**된다.
- 원격 수신 메시지는 **재전송하지 않는다**.
  - 이 정책으로 루프/중복 전송을 방지한다.

### 4.4 Backpressure 정책

- QUEUE 모드: SPOT별 큐가 HWM 초과 시 **해당 SPOT에만 drop**.
- RINGBUFFER 모드: 토픽 로그가 HWM 초과 시 **느린 구독자 커서 skip**.
- drop 여부는 publish 성공/실패에 영향을 주지 않는다.

---

## 5. 메시지 포맷

### 5.1 네트워크 메시지 (PUB/SUB)

PUB/SUB는 **topic prefix**로 필터링한다. 네트워크 메시지 구조는 아래와 같다.

```
Frame 0: topic (string)
Frame 1..N: payload (multipart 가능)
```

- `zmq_spot_publish()`는 **topic + msg** 2프레임으로 전송한다.
- `zmq_spot_recv()`는 **payload 프레임만 반환**하고, topic은 별도로 반환한다.
- Node는 프레임을 추가/수정하지 않는다.

### 5.2 로컬 전달 포맷

- 로컬 큐에는 **topic + payload**를 함께 저장한다.
- `zmq_spot_recv()`는 topic과 payload를 함께 반환한다.

---

## 6. C API 명세

### 6.1 SPOT Node (Agent)

```c
/* SPOT Node 생성/종료 */
ZMQ_EXPORT void *zmq_spot_node_new(void *ctx);
ZMQ_EXPORT int zmq_spot_node_destroy(void **node_p);

/* 클러스터 PUB 바인드 */
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

/* 토픽 생성/삭제 (로컬 설정) */
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
- `zmq_spot_topic_create`: 로컬 설정이 이미 존재하면 `EEXIST`
- `zmq_spot_publish`: 구독자 유무와 무관하게 성공 (topic 규칙 위반 시 `EINVAL`)
- `zmq_spot_subscribe`: 즉시 활성화 (topic 규칙 위반 시 `EINVAL`)
- `zmq_spot_unsubscribe`: exact 또는 pattern 문자열 모두 허용

**정리 규칙**
- `zmq_spot_destroy()`는 해당 SPOT이 보유한
  - 로컬 토픽 설정을 **자동 destroy** 하고
  - 구독/패턴 구독을 **자동 해제**한다.
- 이후 refcount가 0이 되는 토픽은 UNSUBSCRIBE 대상이 된다.

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
| `zmq_spot_topic_create` | CREATE | 로컬 토픽 설정 요청 |
| `zmq_spot_topic_destroy` | DESTROY | 로컬 토픽 설정 제거 |
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

zmq_spot_subscribe(spot2, "chat:room1");

zmq_msg_t msg;
zmq_msg_init_size(&msg, 5);
memcpy(zmq_msg_data(&msg), "hello", 5);
zmq_spot_publish(spot1, "chat:room1", &msg, 0);
```

### 7.2 두 서버, Discovery 기반 연결

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

// SPOT 사용
void *spotA = zmq_spot_new(nodeA);
void *spotB = zmq_spot_new(nodeB);

zmq_spot_subscribe(spotB, "zone:12:state");
zmq_msg_t msg;
zmq_msg_init_size(&msg, 4);
memcpy(zmq_msg_data(&msg), "pong", 4);
zmq_spot_publish(spotA, "zone:12:state", &msg, 0);
```

### 7.3 RingBuffer 토픽 (옵션)

```c
void *spot = zmq_spot_new(node);
zmq_spot_topic_create_ex(spot, "metrics:cluster:cpu", ZMQ_SPOT_TOPIC_RINGBUFFER);
zmq_spot_subscribe(spot, "metrics:cluster:cpu");
```

---

## 8. 구현 계획

1) **Node 데이터 플레인**
   - PUB bind, SUB connect/disconnect
   - Discovery 기반 peer 관리

2) **구독 집계/필터 갱신**
   - refcount, 패턴 필터 처리
   - SUBSCRIBE/UNSUBSCRIBE 업데이트

3) **메시지 전달**
   - 로컬 publish: 로컬 분배 + PUB 전송
   - 원격 수신: 로컬 분배만 수행

4) **수신 큐/링버퍼 구현**
   - per-spot 큐 (기본)
   - per-topic ringbuffer (옵션)

5) **thread-safe wrapper**
   - Node 내부 직렬 처리 큐 연결

---

## 9. 검증 방법

### 9.1 단위/기능 테스트

- `test_spot_pubsub_basic`: 두 노드 publish/subscribe 기본 동작
- `test_spot_sub_refcount`: 동일 토픽 다중 구독 시 SUBSCRIBE 1회만 수행
- `test_spot_unsubscribe_cleanup`: 마지막 구독 해제 시 UNSUBSCRIBE 수행
- `test_spot_pattern_subscribe`: 패턴 구독 동작
- `test_spot_publish_no_subscribers`: 구독자 없어도 publish 성공
- `test_spot_no_republish_loop`: 원격 수신 메시지가 재발행되지 않음
- `test_spot_discovery_connect`: Discovery 추가/제거에 따른 connect/disconnect
- `test_spot_discovery_self_ignore`: Discovery 목록에 자신이 포함돼도 connect 하지 않음
- `test_spot_discovery_pub_endpoint`: Registry에 등록된 PUB endpoint로 SUB 연결됨
- `test_spot_reconnect_resubscribe`: 재연결 시 기존 SUB 필터가 자동 전파됨
- `test_spot_multi_publisher_same_topic`: 동일 토픽 다중 발행 수신 확인
- `test_spot_ringbuffer_basic`: ringbuffer 모드 fan-out 동작
- `test_spot_scale_instances`: 10k SPOT 생성/구독/발행 성능

### 9.2 시나리오 테스트

- **시나리오 1: 노드 추가/제거**
  - 노드 추가 시 자동 연결
  - 제거 시 자동 disconnect 및 로컬 서비스 유지

- **시나리오 2: 토픽 대량 구독**
  - 다수 토픽/패턴 구독 시 필터 업데이트 비용 측정

- **시나리오 3: 대규모 fan-out**
  - QUEUE vs RINGBUFFER 비교

- **시나리오 4: 재연결 구독 유지**
  - Node B 재시작 후 SUB 필터 자동 전파 확인

- **시나리오 5: 다중 발행 충돌**
  - 동일 토픽을 여러 Node가 발행할 때 구독자가 모두 수신하는지 확인

---

## 10. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|-----------|
| 4.1 | 2026-01-28 | PUB/SUB mesh 테스트 항목 보강 |
| 4.0 | 2026-01-28 | SPOT을 PUB/SUB 기반 설계로 전면 변경 (owner/QUERY/ROUTER 제거) |
| 3.5 | 2026-01-27 | ROUTER_HANDOVER 설정 명시 |
| 3.4 | 2026-01-27 | 토픽 충돌 정책을 LWW(owner_epoch) 기준으로 변경 |
| 3.3 | 2026-01-27 | 토픽 owner 매핑 공유 방식(QUERY/ANNOUNCE) 명시 |
| 3.2 | 2026-01-27 | ZLink PUB/SUB 필터링 동작 정리 |
| 3.1 | 2026-01-27 | 구현 상세/테스트 시나리오 보강 |
| 3.0 | 2026-01-27 | SPOT 구조 상세화 (Node 소켓 역할, API 순서, 메시지 포맷 추가) |
| 2.9 | 2026-01-27 | 노드 ID/라우팅, API 순서, QUERY 포맷, 큐/정리 규칙 등 상세 보강 |
| 2.0 | 2026-01-27 | SPOT Instance/Node 분리 모델로 전면 재작성, OWNED/ROUTED 정의, Pending 구독 정책 명시 |
| 1.0 | 2026-01-26 | 초기 버전 |
