[← 목록](README.md) | [시세 분배 →](02-realtime-quote.md)

# MMORPG 존 인접 데이터 공유

> SPOT의 원래 설계 동기.

## 문제

게임 월드를 그리드 존으로 분할하고 각 존을 서버가 담당한다.
플레이어가 존 경계에 있으면 인접 존의 몬스터/플레이어를 볼 수 있어야 한다.
각 존 서버는 인접 존의 상태를 알아야 하지만, 모든 존의 데이터를 받을 필요는 없다.

## 구조

```
  4x4 게임 월드 (16개 존, 4개 서버로 분산)

  ┌──────┬──────┬──────┬──────┐
  │(0,0) │(1,0) │(2,0) │(3,0) │     Server 1: (0,0)~(1,1)
  │  S1  │  S1  │  S2  │  S2  │     Server 2: (2,0)~(3,1)
  ├──────┼──────┼──────┼──────┤     Server 3: (0,2)~(1,3)
  │(0,1) │(1,1) │(2,1) │(3,1) │     Server 4: (2,2)~(3,3)
  │  S1  │  S1  │  S2  │  S2  │
  ├──────┼──────┼──────┼──────┤
  │(0,2) │(1,2) │(2,2) │(3,2) │
  │  S3  │  S3  │  S4  │  S4  │
  ├──────┼──────┼──────┼──────┤
  │(0,3) │(1,3) │(2,3) │(3,3) │
  │  S3  │  S3  │  S4  │  S4  │
  └──────┴──────┴──────┴──────┘

  토픽: "zone:<x>:<y>:state"
```

## 데이터 흐름

```
  Server 1                           Server 2
  ┌────────────────────┐             ┌────────────────────┐
  │                    │             │                    │
  │ publish:           │   mesh      │ subscribe:         │
  │  "zone:1:1:state"  │────────────►│  "zone:1:1:state"  │
  │  (존 경계 엔티티)   │             │  (인접 존 데이터)   │
  │                    │             │                    │
  │ subscribe:         │   mesh      │ publish:           │
  │  "zone:2:1:state"  │◄────────────│  "zone:2:1:state"  │
  │  (인접 존 데이터)   │             │  (존 경계 엔티티)   │
  │                    │             │                    │
  └────────────────────┘             └────────────────────┘

  Server 1의 (1,1) 존과 Server 2의 (2,1) 존이 인접
  → 서로의 경계 엔티티 상태를 토픽으로 교환
```

## 핵심 코드

```c
/* 서버 초기화 */
void *ctx = zlink_ctx_new();
void *discovery = zlink_discovery_new(ctx, ZLINK_SERVICE_TYPE_SPOT, "game-world");
zlink_discovery_connect_registry(discovery, "tcp://registry:5551");

void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9000");
zlink_spot_node_attach_discovery(node, discovery);
void *spot = zlink_spot_new(node);

/* 자기 존 상태를 publish */
char topic[64];
snprintf(topic, sizeof(topic), "zone:%d:%d:state", my_x, my_y);
zlink_publish(spot, topic, &entity_state, 1, 0);

/* 인접 존만 구독 (상하좌우 + 대각선) */
for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
        if (dx == 0 && dy == 0) continue;
        int nx = my_x + dx, ny = my_y + dy;
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
        char adj_topic[64];
        snprintf(adj_topic, sizeof(adj_topic), "zone:%d:%d:state", nx, ny);
        zlink_set_subscription(spot, adj_topic);
    }
}

/* 인접 존 데이터 수신 — poller 루프에서 zlink_spot_subscribe() 로 드레인 */
zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char service_name[64];
size_t service_name_len = sizeof(service_name);
char topic[64];
size_t topic_len = sizeof(topic);

if (zlink_spot_subscribe(spot, &source_rid,
                         &parts, &part_count,
                         service_name, &service_name_len,
                         topic, &topic_len, 0) == ZLINK_RECV_OK) {
    /* topic = "zone:2:1:state" → 인접 존 엔티티 상태 처리 */
    zlink_multipart_close(parts, part_count);
}
```

## 왜 SPOT인가

- 16개 존 × 인접 8방향 = 존마다 다른 구독 조합. 토픽 기반 필터링이 자연스러움
- 서버 추가/제거 시 Discovery가 mesh 자동 조정
- 브로커 경유 없이 서버 간 직접 전달 → 게임 상태 동기화에 중요한 저지연
- 외부 인프라 없이 게임 서버 프로세스 내 임베디드

## 참고

`core/tests/e2e/spot/spot_pubsub_scenario_discovery_cases.cpp` —
`test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery()`
