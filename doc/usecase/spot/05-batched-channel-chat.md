[← 이벤트 전파](04-microservice-event.md) | [목록](README.md)

# 배치 기반 채널 메시징 (채팅)

## 문제

대규모 채팅(1000개 채널 × 100명)에서 메시지마다 즉시 브로드캐스트하면
fan-out이 폭발한다. Redis pub/sub은 싱글 스레드 병목, Kafka는 지연이 높다.
Discord 같은 대규모 시스템은 자체 라우팅을 구현하지만 개발 비용이 크다.

## 핵심 아이디어

채팅 채널을 SPOT node + facade 조합으로 나눈다.

- **aggregator**: 채널당 1개 SPOT node + facade
  (`zlink_spot_node_new` + `zlink_spot_new`).
  채널로 들어오는 메시지를 수집하고, 1~3초 캐싱했다가 한번에 배치 발행한다.
- **receiver**: 서버당 1개 SPOT node + facade
  (`zlink_spot_node_new` + `zlink_spot_new`).
  배치된 메시지를 수신하고, 해당 서버에 접속한 유저에게 전달한다.

facade는 로컬 SPOT Node 위에 붙고, 서버 간 통신은 SPOT Node끼리 tcp mesh로
처리한다.

```
  aggregator_node = zlink_spot_node_new(ctx)
  aggregator      = zlink_spot_new(aggregator_node)   (channel per 1 instance, collect + batch)
  receiver_node   = zlink_spot_node_new(ctx)
  receiver        = zlink_spot_new(receiver_node)     (server per 1 instance, deliver to users)

  ┌──────────────────── Server ────────────────────┐
  │                                                 │
  │  aggregator(ch:1) ─── inproc ──┐               │
  │  aggregator(ch:2) ─── inproc ──┼──► SPOT Node  │
  │  aggregator(ch:3) ─── inproc ──┘       ▲       │
  │                                        │       │
  │  receiver ──────────── inproc ─────────┘       │
  │     │                                          │
  │  User A, B, C (WebSocket / STREAM)             │
  │                                                 │
  └─────────────────────────────────────────────────┘
```

## 메시지 흐름 (10만명 기준)

```
  User A (Receiver 3)    ch:123 aggregator (Aggregator 1)    User C (Receiver 4)

  ┌─────────────────┐    ┌─────────────────────────┐    ┌─────────────────┐
  │ Receiver 3      │    │ Aggregator 1            │    │ Receiver 4      │
  │                 │    │                         │    │                 │
  │ User A          │    │ SPOT "ch:123"           │    │                 │
  │   │             │    │   │                     │    │                 │
  │ (1) publish     │    │ (3) collect + cache     │    │                 │
  │   "ch:123:in"   │    │     1~3 sec             │    │                 │
  │   │             │    │   │                     │    │                 │
  │   ▼             │    │ (4) batch publish       │    │                 │
  │ SPOT Node ──(2)─┼───►│     "ch:123:out"        │    │                 │
  │                 │    │   │                     │    │                 │
  │ SPOT Node ◄─(5)─┼────┤ SPOT Node ──────(5)────┼───►│ SPOT Node       │
  │   │             │    │                         │    │   │             │
  │ (6) receiver    │    │                         │    │ (6) receiver    │
  │   │             │    │                         │    │   │             │
  │ User B          │    │                         │    │ User C          │
  │                 │    │                         │    │                 │
  └─────────────────┘    └─────────────────────────┘    └─────────────────┘

  (1) User A publish "ch:123:in"
  (2) SPOT Node mesh -> Aggregator 1
  (3) aggregator SPOT collect + cache 1~3 sec
  (4) batch publish "ch:123:out"
  (5) SPOT Node mesh -> subscribed Receivers
  (6) receiver SPOT -> local users
```

## 부하 비교

채널 100명, 초당 1건씩 전송 기준:

| 방식 | 네트워크 msg/sec (1채널) | 1,000채널 | 6,000채널 |
|------|:----------------------:|:---------:|:---------:|
| 즉시 브로드캐스트 | 100 × 서버수 | 200만 | 1,200만 |
| 1초 배치 | 1 × 서버수 | 2만 | 12만 |
| 3초 배치 | 0.3 × 서버수 | 7천 | 4만 |

배치로 **100~300배** fan-out 감소.

## 규모별 서버 산정

공통 가정: 그룹당 ~50명, 유저당 평균 3그룹 가입, 동시 활성 10%

### 동접 10만명

```
  Aggregator x2                      Receiver x4
  (6,000 channels, SPOT per ch)      (100,000 users, SPOT per server)

  ┌───────────────────┐
  │ Aggregator 1      │              ┌───────────────────┐
  │ ch:0~2999         │───── mesh ──►│ Receiver 1        │
  │ 3,000 SPOT handles│              │ 25,000 users      │
  │ SPOT Node         │───── mesh ──►│ 1 SPOT handle     │
  └───────────────────┘              ├───────────────────┤
  ┌───────────────────┐              │ Receiver 2        │
  │ Aggregator 2      │───── mesh ──►│ 25,000 users      │
  │ ch:3000~5999      │              │ 1 SPOT handle     │
  │ 3,000 SPOT handles│───── mesh ──►├───────────────────┤
  │ SPOT Node         │              │ Receiver 3        │
  └───────────────────┘              │ 25,000 users      │
                                     │ 1 SPOT handle     │
                                     ├───────────────────┤
                                     │ Receiver 4        │
                                     │ 25,000 users      │
                                     │ 1 SPOT handle     │
                                     └───────────────────┘

  total: 6 servers
```

| 항목 | 수치 |
|------|------|
| 총 그룹 | 6,000개 (활성 ~600개) |
| 메시지 발생률 | ~1,000 msg/sec |
| Aggregator 서버 | **2대** (3,000 SPOT handles/서버) |
| Receiver 서버 | **4대** (25,000 users/서버, 1 SPOT handle) |
| 총 서버 | **6대** |
| 네트워크 부하 (1초 배치) | ~3,000 msg/sec |

### 동접 100만명

```
  Aggregator x6                      Receiver x20
  (60,000 channels, SPOT per ch)     (1,000,000 users, SPOT per server)

  ┌───────────────────┐
  │ Aggregator 1      │              ┌───────────────────┐
  │ ch:0~9999         │───── mesh ──►│ Receiver 1        │
  │ 10,000 SPOT       │              │ 50,000 users      │
  │ SPOT Node         │              │ 1 SPOT handle     │
  └───────────────────┘              ├───────────────────┤
  ┌───────────────────┐              │ Receiver 2        │
  │ Aggregator 2      │───── mesh ──►│ 50,000 users      │
  │ ch:10000~19999    │              │ 1 SPOT handle     │
  │ 10,000 SPOT       │              ├───────────────────┤
  │ SPOT Node         │              │ ...               │
  └───────────────────┘              ├───────────────────┤
  ┌───────────────────┐              │ Receiver 20       │
  │ ...               │───── mesh ──►│ 50,000 users      │
  ├───────────────────┤              │ 1 SPOT handle     │
  │ Aggregator 6      │              └───────────────────┘
  │ ch:50000~59999    │
  │ 10,000 SPOT       │
  │ SPOT Node         │
  └───────────────────┘

  total: 26 servers
```

| 항목 | 수치 |
|------|------|
| 총 그룹 | 60,000개 (활성 ~6,000개) |
| 메시지 발생률 | ~10,000 msg/sec |
| Aggregator 서버 | **6대** (10,000 SPOT facades/서버) |
| Receiver 서버 | **20대** (50,000 users/서버, 1 SPOT facade) |
| 총 서버 | **26대** |
| 네트워크 부하 (1초 배치) | ~60,000 msg/sec |

## 핵심 코드

```c
/* ── aggregator: 채널당 1개 SPOT node + facade ── */
/* 채널 "ch:123"의 메시지를 수집하고 배치 발행 */

void *aggr_node = zlink_spot_node_new(ctx);
void *aggr = zlink_spot_new(aggr_node);  /* node 위의 SPOT facade */

zlink_set_subscription(aggr, "ch:123:in");

void on_collect(const zlink_routing_id_t *rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count, void *ud)
{
    batch_t *b = (batch_t *)ud;
    buffer_append(b, parts, part_count);

    if (should_flush(b)) {
        zlink_publish(aggr, "ch:123:out", b->msgs, b->count, 0);
        buffer_reset(b);
    }
}
zlink_subscribe_handler(aggr, on_collect, &batch);


/* ── receiver: 서버당 1개 SPOT node + facade ── */
/* 이 서버 유저가 속한 채널의 배치를 수신하여 전달 */

void *recv_node = zlink_spot_node_new(ctx);
void *recv_spot = zlink_spot_new(recv_node);  /* node 위의 SPOT facade */

zlink_set_subscription(recv_spot, "ch:123:out");
zlink_set_subscription(recv_spot, "ch:456:out");

void on_deliver(const zlink_routing_id_t *rid,
                const char *topic, size_t topic_len,
                zlink_msg_t *parts, size_t part_count, void *ud)
{
    deliver_to_local_users(topic, parts, part_count);
}
zlink_subscribe_handler(recv_spot, on_deliver, NULL);


/* ── 유저가 메시지 전송 시 (어느 receiver 서버에서든) ── */

zlink_msg_t msg;
zlink_msg_init_size(&msg, len);
memcpy(zlink_msg_data(&msg), text, len);
zlink_publish(user_spot, "ch:123:in", &msg, 1, 0);
/* SPOT mesh -> ch:123 aggregator -> batch -> receivers */
```

## 필터링 활용

배치 발행 시 메시지 유형별 토픽 세분화:

```c
/* aggregator가 유형별로 배치 분리 후 발행 */
zlink_publish(aggr, "ch:123:out:text", text_batch, count, 0);
zlink_publish(aggr, "ch:123:out:image", image_batch, count, 0);
zlink_publish(aggr, "ch:123:out:system", system_batch, count, 0);

/* receiver는 관심 유형만 구독 */
zlink_set_subscription(recv_spot, "ch:123:out:text");
zlink_set_subscription(recv_spot, "ch:123:out:system");
/* 이미지 미구독 -> 트래픽 절감 */
```

## 왜 SPOT인가

- **배치로 fan-out 100~300배 감소** — pub/sub 브로드캐스트의 근본 문제 해결
- **채널 = SPOT 인스턴스** — 채널당 aggregator SPOT, 서버당 receiver SPOT으로 자연스러운 매핑
- **경량 구조** — aggregator는 전부 inproc 핸들. 네트워크는 SPOT Node mesh만
- **brokerless** — Redis/Kafka 없이 SPOT mesh만으로 동작
- **구독 필터링** — 매칭되는 receiver 서버에만 전송

## 고려사항

- **1~3초 지연**: 채팅에서는 수용 가능. 실시간 게임 상태에는 부적합
- **aggregator 장애**: 담당 서버 장애 시 해당 채널 배치 중단 → failover/재배정 필요
- **메시지 순서**: 채널당 aggregator가 하나이므로 채널 내 순서는 보존
- **히스토리**: SPOT은 live 전달만. 재접속 catch-up은 DB + sequence 번호로 별도 구현 필요
