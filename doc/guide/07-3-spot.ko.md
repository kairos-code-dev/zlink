[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT 토픽 PUB/SUB (위치투명 발행/구독)

> 이 가이드는 recv-first public surface 기준으로 작성되었다.
> `SpotNode`와 unified `Spot`은 recv 모드로 시작하고,
> `zlink_subscribe_handler()`로 callback 모드로 일방 전환된다.

## 1. 개요

SPOT은 위치 투명한 토픽 기반 발행/구독 시스템이다. Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여, 클러스터 전체에서 토픽 메시지를 발행/구독할 수 있다.

> **명칭에 대하여**: SPOT은 "위치(spot)"에서 유래한 이름이다. 각 객체(노드)가 자신의 위치에서 토픽을 발행하고, 다른 위치의 토픽을 구독하는 객체 단위의 위치투명한(location-transparent) pub/sub 메시 시스템이다.

### 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Node** | PUB/SUB Mesh 참여 에이전트 (노드별 1개) |
| **SPOT Pub** | 토픽 발행 경로 (`spot` / `spot_node`의 hot path) |
| **SPOT Sub** | 토픽 구독/수신 핸들 |
| **Topic** | 문자열 키 기반 메시지 채널 |
| **Pattern** | 접두어 + `*` 와일드카드 구독 |
| **Handler** | 메시지 수신 시 자동 호출되는 콜백 함수 |

## 2. 아키텍처

### 로컬 publish — 같은 노드 안에서 전달

```
  SpotPub           SPOT Node            SpotSub
    │               (worker)               │
    │  ── publish ──►  │                   │
    │    (inproc)      │                   │
    │                  │ ── callback ─────► │
    │                  │    (inproc)        │
```

SpotPub이 publish하면 SPOT Node 내부 worker가 받아서 같은 노드의 SpotSub에게
callback으로 바로 전달한다.

### 원격 전파 — 클러스터 노드 간 전달

```
  SpotPub          Node 1              Node 2           SpotSub
  (Node 1)        (worker)            (worker)          (Node 2)
    │                │                   │                  │
    │ ── publish ──► │                   │                  │
    │   (inproc)     │                   │                  │
    │                │ ── PUB ─────────► │                  │
    │                │   (tcp mesh)      │                  │
    │                │                   │ ── callback ───► │
    │                │                   │    (inproc)      │
```

로컬 publish는 worker가 두 갈래로 분기한다:
1. 같은 노드의 SpotSub에게 전달 (위의 로컬 경로)
2. mesh PUB 소켓으로 원격 노드에 송출

원격 노드의 worker는 mesh에서 수신한 메시지를 자기 SpotSub에게만 전달하고,
**다시 mesh로 재발행하지 않는다** (루프 방지).

### 전체 구조 요약

```
┌─────────── Node 1 ───────────┐     ┌─────────── Node 2 ───────────┐
│                               │     │                               │
│  SpotPub ──► worker ──► SpotSub │     │  SpotPub ──► worker ──► SpotSub │
│                 │             │     │                 ▲             │
│                 │ PUB         │     │            SUB  │             │
│                 └──── tcp ────┼────►┼─────────────────┘             │
│                 ▲             │     │                 │             │
│            SUB  │             │     │                 │ PUB         │
│                 └──── tcp ────┼◄────┼─────────────────┘             │
│                               │     │                               │
└───────────────────────────────┘     └───────────────────────────────┘
```

- 각 Node의 worker는 **PUB 소켓**으로 송출하고, 다른 노드의 **SUB 소켓**으로 수신한다
- 로컬 publish만 mesh로 나가고, 원격 수신은 재발행하지 않는다 (루프 방지)
- Discovery 연결 시 이 mesh 토폴로지가 자동 구성된다

## 3. SPOT Node 설정

### 3.1 Discovery 기반 자동 Mesh

```c
void *ctx = zlink_ctx_new();

/* Discovery 설정 (peer 발견 + registry uplink / heartbeat owner) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");

/* SPOT Node 설정 */
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_spot_node_bind(node, "tcp://*:9000");

/* Discovery 연결 */
zlink_spot_node_attach_discovery(node, discovery);
```

**주의:** `attach_discovery()`는 bind 이후에 호출하는 것을 권장한다.
Discovery가 attach되면 Registry를 통해 자동으로 peer를 발견하고 연결한다.

### 3.2 수동 Mesh

```c
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_spot_node_bind(node, "tcp://*:9000");

/* 다른 노드의 PUB에 직접 연결 */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

**주의:** 수동 Mesh에서는 Discovery가 없으므로 Registry topology visibility도
없다. 이는 의도된 제한이다.

## 4. Unified SPOT 사용

### 4.1 생성

```c
void *spot = zlink_spot_new(node);
```

`zlink_spot_new()`는 publish와 subscribe를 함께 가진 unified facade를
반환한다. public standalone `spot_pub` / `spot_sub` 생성자는 제공하지 않는다.

### 4.2 발행

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 11);
memcpy(zlink_msg_data(&part), "hello world", 11);
zlink_publish(spot, "chat:room1:message", &part, 1, 0);
```

### 4.3 구독 / 해제

```c
zlink_set_subscription(spot, "chat:room1:message");
zlink_set_subscription(spot, "chat:room1:*");

zlink_unset_subscription(spot, "chat:room1:message");
zlink_unset_subscription(spot, "chat:room1:*");
```

### 4.4 메시지 수신

`SpotNode`와 unified `Spot` 모두 **recv 모드**로 시작한다. 메시지를 직접
수신하거나, **callback 모드**로 한 번 전환할 수 있다. 두 모델은 handle 수명
동안 상호 배타적이다.

#### Recv 모드 (기본)

recv 모드에서는 `zlink_subscribe()`로 메시지를 직접 수신한다.

```c
void *spot = zlink_spot_new(node);
zlink_set_subscription(spot, "chat:room1:message");

/* 다음 메시지 수신 */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic_buf[256];
size_t topic_len = sizeof(topic_buf);
int rc = zlink_subscribe(spot, &source_rid, &parts, &part_count,
                              topic_buf, &topic_len, 0);
if (rc == 0) {
    printf("토픽: %.*s, 파트: %zu\n",
           (int)topic_len, topic_buf, part_count);
    for (size_t i = 0; i < part_count; i++)
        zlink_msg_close(&parts[i]);
}
```

#### Callback 모드

`zlink_subscribe_handler()`를 호출하면 recv 모드에서 callback 모드로
일방 전환된다. 이후 수신 메시지는 설치된 callback으로 자동 dispatch된다.

```c
/* 콜백 함수 정의 */
void on_message(const zlink_routing_id_t *source_rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("토픽: %.*s, 파트: %zu\n", (int)topic_len, topic, part_count);
}

/* spot_node 생성 시 handler 등록 */
void *node = zlink_spot_node_new(ctx, "spot-node");
zlink_subscribe_handler(node, on_message, NULL);

/* 또는 unified spot 생성 시 handler 등록 */
void *spot = zlink_spot_new(node);
zlink_subscribe_handler(spot, on_message, NULL);
```

**중요:** 하나의 `spot` / `spot_node` handle을 여러 스레드에서 동시에
사용할 수 있다 (thread-safe). `publish`는 hot path로서 여러 스레드에서 동시 호출을 허용하고,
subscribe/unsubscribe/attach/peer connect/monitor는 runtime control path로
호출할 수 있다. 다만 callback은 I/O 경로에서 직접 호출되므로, 느린 처리는
사용자 queue로 넘겨 별도 thread에서 처리하는 편이 안전하다.

**제약 사항:**

- recv 모드에서는 `zlink_subscribe()`를 사용한다
- callback 모드 전환은 `zlink_subscribe_handler()`로 한 번만 수행한다
- callback 모드에서 `recv()` 호출은 `EBUSY`로 실패한다
- recv 모드에서 `send_ready_handler()`는 `EBUSY`로 실패한다
- 전환 후 callback 교체나 해제는 지원하지 않는다
- 콜백은 소켓 dispatch / I/O 경로에서 직접 호출된다
- 콜백에서 블로킹 작업을 수행하면 다른 I/O 진행에 영향을 줄 수 있다
- 느린 처리가 필요하면 콜백 안에서 사용자 queue로 넘기고 별도 thread에서 처리한다
- `destroy`는 fail-fast lifecycle gate를 가지므로, 외부 사용을 중단한 뒤
  정리하는 것이 가장 단순하다

> 전체 three-tier 계약과 추가 패턴은 [스레드 안전성 가이드](11-thread-safety.ko.md)를 참고.

## 5. 토픽 규칙

### 명명 규칙

`<domain>:<entity>:<action>` 형식 권장.

예시:
- `chat:room1:message`
- `metrics:zone1:cpu`
- `game:world1:player_move`

### 패턴 구독 규칙

- `*`는 한 개만 허용, 문자열 끝에만
- 대소문자 구분
- 예: `chat:*` → `chat:room1:message`, `chat:room2:join` 모두 매칭

## 6. 전달 정책

- 로컬 publish (`spot`) → 로컬 SPOT Sub 분배 + PUB 송출 (원격 전파)
- 원격 수신 (SUB) → 로컬 SPOT Sub 분배만 (재발행 없음)
- 재발행 없음으로 메시지 루프/중복 방지
- `subscribe()` / `unsubscribe()` 반환은 local socket filter 적용 의미이며,
  클러스터 전체 전파 완료를 보장하지 않는다
- 같은 `spot` handle에서 연속 publish된 메시지의 순서는 보존된다
- 서로 다른 `spot` handle 사이의 전역 순서는 보장하지 않는다
- 동일 subscriber에 exact topic + pattern이 둘 다 매칭되더라도 메시지는 1회만 전달된다

SPOT은 live pub/sub이며, durable delivery, ack/retry, exactly-once,
late join에 대한 과거 메시지 재전송은 보장하지 않는다.

## 7. 정리

```c
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```

**정리 순서:** `spot`을 먼저 destroy하고, 그 다음 `SpotNode`, 마지막으로
`Discovery` 순서로 정리한다. `SpotNode` destroy 전에 관련 `spot`의 외부 사용을
중단해야 한다.

---
[← Gateway](07-2-gateway.ko.md) | [Registry →](07-4-registry.ko.md) | [Routing ID →](08-routing-id.ko.md)
