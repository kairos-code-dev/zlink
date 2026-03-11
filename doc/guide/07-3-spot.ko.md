[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT 토픽 PUB/SUB (위치투명 발행/구독)

> 이 가이드는 callback-only direct-dispatch surface 기준으로 작성되었다.
> 모든 수신은 생성 시 등록한 handler callback으로 dispatch된다.

## 1. 개요

SPOT은 위치 투명한 토픽 기반 발행/구독 시스템이다. Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여, 클러스터 전체에서 토픽 메시지를 발행/구독할 수 있다.

> **명칭에 대하여**: SPOT은 "위치(spot)"에서 유래한 이름이다. 각 객체(노드)가 자신의 위치에서 토픽을 발행하고, 다른 위치의 토픽을 구독하는 객체 단위의 위치투명한(location-transparent) pub/sub 메시 시스템이다.

### 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Node** | PUB/SUB Mesh 참여 에이전트 (노드별 1개) |
| **SPOT Pub** | 토픽 발행 핸들 (thread-safe) |
| **SPOT Sub** | 토픽 구독/수신 핸들 |
| **Topic** | 문자열 키 기반 메시지 채널 |
| **Pattern** | 접두어 + `*` 와일드카드 구독 |
| **Handler** | 메시지 수신 시 자동 호출되는 콜백 함수 |

## 2. 아키텍처

### 단일 서버

```
┌─────────────────────────────────────────────┐
│                 SPOT Node                    │
│  ┌──────────┐         ┌──────────┐          │
│  │ SpotPub  │         │ SpotSub  │          │
│  │ pub:chat │         │ sub:chat │          │
│  └────┬─────┘         └────▲─────┘          │
│       │    inproc          │    inproc       │
│       v                    │                 │
│  [ data plane worker (proxy forwarding) ]    │
└─────────────────────────────────────────────┘
```

- `SpotPub`는 내부 `PUB` facade socket을 통해 data plane으로 publish한다
- `SpotSub`는 내부 `SUB` facade socket을 통해 data plane에서 메시지를 받는다
- data plane worker가 local fanout과 remote mesh forwarding을 proxy 방식으로 수행한다

### 클러스터 (PUB/SUB Mesh)

```
┌──────────┐     PUB/SUB      ┌──────────┐
│  Node 1  │◄───────────────►│  Node 2  │
│  PUB+SUB │                  │  PUB+SUB │
└──────────┘                  └──────────┘
      ▲                            ▲
      │         PUB/SUB            │
      └────────────────────────────┘

┌──────────┐
│  Node 3  │
│  PUB+SUB │
└──────────┘
```

각 Node의 data plane worker가 local publish를 remote mesh로,
remote mesh 수신을 local subscriber로 proxy-style forwarding한다.

## 3. SPOT Node 설정

### 3.1 Discovery 기반 자동 Mesh

```c
void *ctx = zlink_ctx_new();

/* Discovery 설정 (peer 발견 + registry uplink / heartbeat owner) */
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_SPOT);
zlink_discovery_connect_registry(discovery, "tcp://registry1:5551");
zlink_discovery_subscribe(discovery, "spot-node");

/* SPOT Node 설정 (service_name과 handler를 생성 시점에 고정) */
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);
zlink_spot_node_bind(node, "tcp://*:9000");

/* Discovery 연결 */
zlink_spot_node_attach_discovery(node, discovery);
```

**주의:** `attach_discovery()`는 bind 이후에 호출하는 것을 권장한다.
Discovery가 attach되면 Registry를 통해 자동으로 peer를 발견하고 연결한다.

### 3.2 수동 Mesh

```c
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);
zlink_spot_node_bind(node, "tcp://*:9000");

/* 다른 노드의 PUB에 직접 연결 */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

**주의:** 수동 Mesh에서는 Discovery가 없으므로 Registry topology visibility도
없다. 이는 의도된 제한이다.

## 4. SPOT Pub/Sub 사용

### 4.1 발행 (SPOT Pub)

```c
void *pub = zlink_spot_pub_new(node);

/* 멀티파트 메시지 구성 및 발행 */
zlink_msg_t part;
zlink_msg_init_size(&part, 11);
memcpy(zlink_msg_data(&part), "hello world", 11);
zlink_spot_pub_publish(pub, "chat:room1:message", &part, 1, 0);
```

### 4.2 구독 (SPOT Sub)

```c
void *sub = zlink_spot_sub_new(node, on_message);

/* 정확한 토픽 구독 */
zlink_spot_sub_subscribe(sub, "chat:room1:message");

/* 패턴 구독 (접두어 매칭) */
zlink_spot_sub_subscribe_pattern(sub, "chat:room1:*");
```

수신은 `on_message` callback으로 자동 dispatch된다 (아래 [4.4 콜백 핸들러](#44-콜백-핸들러-handler) 참조).

### 4.3 구독 해제

```c
zlink_spot_sub_unsubscribe(sub, "chat:room1:message");
zlink_spot_sub_unsubscribe(sub, "chat:room1:*");
```

### 4.4 콜백 핸들러 (Handler)

`spot_sub` 또는 `spot_node`를 생성할 때 callback을 함께 넘기면, 이후 수신
메시지는 그 callback으로 자동 dispatch된다.

```c
/* 콜백 함수 정의 */
void on_message(const zlink_routing_id_t *source_rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count)
{
    printf("토픽: %.*s, 파트: %zu\n", (int)topic_len, topic, part_count);
}

/* spot_node 생성 시 handler 등록 */
void *node = zlink_spot_node_new(ctx, "spot-node", on_message);

/* 또는 별도 spot_sub 생성 시 handler 등록 */
void *sub = zlink_spot_sub_new(node, on_message);
```

**중요:** `spot_pub`은 thread-safe로 사용할 수 있다. 반면 `spot_sub`은 동일
인스턴스를 여러 스레드에서 동시에 사용하면 안 된다.
subscribe/unsubscribe 조작은 한 번에 하나의 실행 컨텍스트에서만 수행해야 한다.

**제약 사항:**

- callback은 `zlink_spot_node_new()` 또는 `zlink_spot_sub_new()` 호출 시점에 제공해야 한다
- 생성 후 callback 교체나 해제는 지원하지 않는다
- 콜백은 소켓 dispatch / I/O 경로에서 직접 호출된다
- 콜백에서 블로킹 작업을 수행하면 다른 I/O 진행에 영향을 줄 수 있다
- 느린 처리가 필요하면 콜백 안에서 사용자 queue로 넘기고 별도 thread에서 처리한다

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

- 로컬 publish (`spot_pub`) → 로컬 SPOT Sub 분배 + PUB 송출 (원격 전파)
- 원격 수신 (SUB) → 로컬 SPOT Sub 분배만 (재발행 없음)
- 재발행 없음으로 메시지 루프/중복 방지
- `subscribe()` / `unsubscribe()` 반환은 local socket filter 적용 의미이며,
  클러스터 전체 전파 완료를 보장하지 않는다
- 같은 `SpotPub` 인스턴스에서 연속 publish된 메시지의 순서는 보존된다
- 서로 다른 `SpotPub` 인스턴스 사이의 전역 순서는 보장하지 않는다
- 동일 subscriber에 exact topic + pattern이 둘 다 매칭되더라도 메시지는 1회만 전달된다

SPOT은 live pub/sub이며, durable delivery, ack/retry, exactly-once,
late join에 대한 과거 메시지 재전송은 보장하지 않는다.

## 7. 정리

```c
zlink_spot_pub_destroy(&pub);
zlink_spot_sub_destroy(&sub);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```

**정리 순서:** `SpotPub` / `SpotSub`를 먼저 destroy하고, 그 다음 `SpotNode`,
마지막으로 `Discovery` 순서로 정리한다. `SpotNode` destroy 전에 관련
`SpotPub` / `SpotSub`의 외부 사용을 중단해야 한다.

---
[← Gateway](07-2-gateway.ko.md) | [Routing ID →](08-routing-id.ko.md)
