# SPOT 토픽 PUB/SUB (위치투명 발행/구독)

## 1. 개요

SPOT은 위치 투명한 토픽 기반 발행/구독 시스템이다. Discovery 기반으로 PUB/SUB Mesh를 자동 구성하여, 클러스터 전체에서 토픽 메시지를 발행/구독할 수 있다.

### 핵심 용어

| 용어 | 설명 |
|------|------|
| **SPOT Node** | PUB/SUB Mesh 참여 에이전트 (노드별 1개) |
| **SPOT Instance** | 토픽 발행/구독 핸들 (Node 내 여러 개) |
| **Topic** | 문자열 키 기반 메시지 채널 |
| **Pattern** | 접두어 + `*` 와일드카드 구독 |

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

## 4. SPOT Instance 사용

### 4.1 발행

```c
void *spot = zlink_spot_new(node);

/* 발행 */
zlink_msg_t msg;
zlink_msg_init_data(&msg, "hello world", 11, NULL, NULL);
zlink_spot_publish(spot, "chat:room1:message", &msg, 1, 0);
```

### 4.2 구독

```c
void *spot = zlink_spot_new(node);

/* 정확한 토픽 구독 */
zlink_spot_subscribe(spot, "chat:room1:message");

/* 패턴 구독 (접두어 매칭) */
zlink_spot_subscribe_pattern(spot, "chat:room1:*");

/* 수신 */
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic[256];
size_t topic_len = 256;
zlink_spot_recv(spot, &parts, &part_count, 0, topic, &topic_len);

printf("토픽: %.*s\n", (int)topic_len, topic);
zlink_msgv_close(parts, part_count);
```

### 4.3 구독 해제

```c
zlink_spot_unsubscribe(spot, "chat:room1:message");
zlink_spot_unsubscribe(spot, "chat:room1:*");
```

## 5. 토픽 모델

### 명명 규칙

`<domain>:<entity>:<action>` 형식 권장.

예시:
- `chat:room1:message`
- `metrics:zone1:cpu`
- `game:world1:player_move`

### 토픽 모드

```c
/* QUEUE 모드 (기본): per-spot 큐 */
zlink_spot_topic_create(spot, "chat:room1", ZLINK_SPOT_TOPIC_QUEUE);

/* RINGBUFFER 모드: 토픽 로그 append */
zlink_spot_topic_create(spot, "metrics:cpu", ZLINK_SPOT_TOPIC_RINGBUFFER);
```

| 모드 | 설명 |
|------|------|
| QUEUE | SPOT Instance별 독립 큐 (기본) |
| RINGBUFFER | 토픽 단위 링 버퍼, 최신 메시지 유지 |

### 패턴 구독 규칙

- `*`는 한 개만 허용, 문자열 끝에만
- 대소문자 구분
- 예: `chat:*` → `chat:room1:message`, `chat:room2:join` 모두 매칭

## 6. 전달 정책

- 로컬 publish → 로컬 SPOT 분배 + PUB 송출 (원격 전파)
- 원격 수신 (SUB) → 로컬 SPOT 분배만 (재발행 없음)
- 재발행 없음으로 메시지 루프/중복 방지

## 7. 정리

```c
zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_discovery_destroy(&discovery);
```
