# SPOT 토픽 PUB/SUB (위치투명 발행/구독)

## 1. 개요

SPOT은 위치 투명한 토픽 기반 발행/구독 시스템이다. Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여, 클러스터 전체에서 토픽 메시지를 발행/구독할 수 있다.

### 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Node** | PUB/SUB Mesh 참여 에이전트 (노드별 1개) |
| **SPOT Pub** | 토픽 발행 핸들 (기본 thread-safe) |
| **SPOT Sub** | 토픽 구독/수신 핸들 |
| **Topic** | 문자열 키 기반 메시지 채널 |
| **Pattern** | 접두어 + `*` 와일드카드 구독 |
| **Handler** | 메시지 수신 시 자동 호출되는 콜백 함수 |

## 2. 아키텍처

### 단일 서버

```
┌─────────────────────────────────────┐
│           SPOT Node                  │
│  ┌──────────┐  ┌──────────┐         │
│  │ SPOT A   │  │ SPOT B   │         │
│  │ pub:chat │  │ sub:chat │         │
│  └──────────┘  └──────────┘         │
└─────────────────────────────────────┘
```

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

## 3. SPOT Node 설정

### 3.1 Discovery 기반 자동 Mesh

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
void *discovery = zlink_discovery_new(ctx);

/* Registry 연결 */
zlink_discovery_connect_registry(discovery, "tcp://registry1:5550");
zlink_discovery_subscribe(discovery, "spot-node");

/* PUB bind */
zlink_spot_node_bind(node, "tcp://*:9000");

/* Registry에 등록 */
zlink_spot_node_connect_registry(node, "tcp://registry1:5551");
zlink_spot_node_register(node, "spot-node", NULL);

/* Discovery 기반 peer 자동 연결 */
zlink_spot_node_set_discovery(node, discovery, "spot-node");
```

### 3.2 수동 Mesh

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");

/* 다른 노드의 PUB에 직접 연결 */
zlink_spot_node_connect_peer_pub(node, "tcp://node2:9000");
zlink_spot_node_connect_peer_pub(node, "tcp://node3:9000");
```

## 4. SPOT Pub/Sub 사용

### 4.1 발행 (SPOT Pub)

```c
void *pub = zlink_spot_pub_new(node);

/* 발행 */
zlink_msg_t msg;
zlink_msg_init_data(&msg, "hello world", 11, NULL, NULL);
zlink_spot_pub_publish(pub, "chat:room1:message", &msg, 1, 0);
```

### 4.2 구독/수신 (SPOT Sub)

```c
void *sub = zlink_spot_sub_new(node);

/* 정확한 토픽 구독 */
zlink_spot_sub_subscribe(sub, "chat:room1:message");

/* 패턴 구독 (접두어 매칭) */
zlink_spot_sub_subscribe_pattern(sub, "chat:room1:*");

/* 수신 */
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic[256];
size_t topic_len = 256;
zlink_spot_sub_recv(sub, &parts, &part_count, 0, topic, &topic_len);

printf("토픽: %.*s\n", (int)topic_len, topic);
zlink_msgv_close(parts, part_count);
```

### 4.3 구독 해제

```c
zlink_spot_sub_unsubscribe(sub, "chat:room1:message");
zlink_spot_sub_unsubscribe(sub, "chat:room1:*");
```

### 4.4 Raw 소켓 노출 정책

- `spot_pub`은 raw socket을 노출하지 않는다.
- `spot_sub`은 raw SUB socket 노출 API를 제공한다.

```c
void *raw_sub = zlink_spot_sub_socket(sub);
```

### 4.5 콜백 핸들러 (Handler)

`recv()` 대신 콜백 함수를 등록하면 메시지 도착 시 자동으로 호출된다.

```c
/* 콜백 함수 정의 */
void on_message(const char *topic, size_t topic_len,
                const zlink_msg_t *parts, size_t part_count,
                void *userdata)
{
    printf("토픽: %.*s, 파트: %zu\n", (int)topic_len, topic, part_count);
}

/* 핸들러 등록 */
zlink_spot_sub_set_handler(sub, on_message, NULL);

/* 핸들러 해제 (inflight 콜백 완료 대기 후 반환) */
zlink_spot_sub_set_handler(sub, NULL, NULL);
```

**제약 사항:**

- handler가 활성 상태이면 `recv()` 호출 시 `EINVAL` 반환 (상호 배타)
- `NULL` 전달로 핸들러를 해제하면, 진행 중인 콜백이 모두 완료된 후 반환
- 콜백은 spot_node 워커 스레드에서 호출된다

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

## 7. 정리

```c
zlink_spot_pub_destroy(&pub);
zlink_spot_sub_destroy(&sub);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```
