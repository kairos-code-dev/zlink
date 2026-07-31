[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

<!-- zlink-nav:start -->
[← 서비스](07-0-services.ko.md) | [SPOT Actor →](07-4-actor.ko.md)
<!-- zlink-nav:end -->

# SPOT 사용 가이드

이 문서는 애플리케이션 개발자가 SPOT을 어떻게 쓰는지 설명한다.
정확한 함수 계약은 [SPOT spec](../api/spot.ko.md)를 본다.

> SPOT을 **언제·왜** 쓰는지(raw 소켓·Actor와의 관계, 실행 직렬성)는
> [서비스 개요 §멘탈 모델](07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)에서
> 먼저 잡고 오면 이 문서의 how-to가 더 잘 읽힌다.

## 1. SPOT이 하는 일

SPOT은 `SpotNode`와 `Spot` 두 층으로 나뉜다.

- `SpotNode`
  노드 토폴로지, 수동 피어 연결, route bridge, 외부 발행 유입(publish ingress)을
  관리한다.
- `Spot`
  애플리케이션이 실제로 쓰는 facade(facade, 단순화된 인터페이스)다. 토픽 발행/구독, 라우팅 수신,
  채널 전송/요청을 제공한다.

일반적인 순서는 다음과 같다.

1. `SpotNode`를 만든다.
2. bind하거나 토폴로지에 필요한 raw peer를 연결한다.
3. 외부 channel runtime이 SPOT route를 넘겨야 하면 route bridge를 만든다.
4. `Spot` facade를 만든다.
5. `Spot`으로 publish/subscribe 또는 채널 호출을 사용한다.

`zlink_spot_new()`가 성공하면 해당 `Spot`의 routed recv 평면은 이미 준비된 상태다.
첫 `zlink_spot_recv()` 호출이 숨겨진 activation이나 자원 생성을 수행한다고
가정하면 안 된다.

## 2. 가장 단순한 흐름

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx, NULL);
zlink_spot_node_set_pub_bind(node, "tcp://127.0.0.1:7001");

void *spot = zlink_spot_new(node);

zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "hello", 5);

zlink_spot_publish(spot, "price.usdkrw", &msg, 1, 0);
zlink_msg_close(&msg);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_ctx_term(&ctx);
```

`NULL`을 넘기면 topic 기능과 routed 기능을 모두 켠다 — `ZLINK_SPOT_NODE_MODE_ALL`과
동일하다. 한쪽 기능만 필요한 프로세스라면 생성 시점에 `zlink_spot_node_options_t`를
넘긴다.

```c
zlink_spot_node_options_t opts = {
  .mode = ZLINK_SPOT_NODE_MODE_PUBSUB
};
void *node = zlink_spot_node_new(ctx, &opts);
```

세 가지 mode 값의 차이는 아래와 같다.

| mode 상수 | 효과 |
|---|---|
| `ZLINK_SPOT_NODE_MODE_ALL` (또는 `NULL`) | topic publish/subscribe와 routed request/reply 모두 사용 가능 |
| `ZLINK_SPOT_NODE_MODE_PUBSUB` | topic publish/subscribe만 사용. routed API는 `ENOTSUP`으로 실패 |
| `ZLINK_SPOT_NODE_MODE_ROUTED` | routed request/reply만 사용. topic API는 `ENOTSUP`으로 실패 |

꺼진 기능은 내부 socket을 생성하지 않는다 — 사용하지 않는 기능에 대한 숨은 자원 비용이 없다.

이 예제는 한 프로세스 안에서 SPOT 노드를 만들고 통합 `Spot` facade로
토픽 하나를 발행하는 최소 흐름이다.

### 2.1 room id가 정해진 Spot 확보

게임 방이나 작업 그룹처럼 애플리케이션이 이미 room id를 알고 있는 경우에는
`zlink_spot_node_spot_get_or_new()`를 사용한다. 이 함수는 "있으면 가져오고,
없으면 만든다"는 흐름을 `SpotNode` 안에서 처리한다. 사용자 코드가 lookup 후
새로 만들고 routing id를 다시 설정하는 순서를 직접 작성하지 않아도 된다.

```c
zlink_routing_id_t room_rid;
memset(&room_rid, 0, sizeof(room_rid));
room_rid.size = 8;
memcpy(room_rid.data, "room-001", 8);

void *room = NULL;
uint32_t created = 0;
zlink_config_result_t rc =
  zlink_spot_node_spot_get_or_new(node, &room_rid, &room, &created);

if (rc == ZLINK_CONFIG_OK && created) {
  /* 최초 생성자만 방 초기 상태를 구성한다. */
}
```

반환된 `room`은 일반 `Spot` facade와 같은 방식으로 사용하고
`zlink_spot_destroy()`로 닫는다. actor를 방에 참가시키는 작업은 이 함수가
수행하지 않는다. Spot 확보와 actor join을 분리해야 "방이 없어서 만들었다"와
"방에는 도달했지만 참가가 거절되었다"를 애플리케이션이 다르게 처리할 수 있다.

## 3. Node를 네트워크에 올리는 방법

### 3.1 수동 피어 연결

고정된 엔드포인트를 알고 있으면 노드끼리 직접 연결할 수 있다.

```c
void *a = zlink_spot_node_new(ctx, NULL);
void *b = zlink_spot_node_new(ctx, NULL);

zlink_spot_node_set_pub_bind(a, "tcp://127.0.0.1:7101");
zlink_spot_node_set_pub_bind(b, "tcp://127.0.0.1:7102");

zlink_spot_node_connect_peer(a, "tcp://127.0.0.1:7102");
zlink_spot_node_connect_peer(b, "tcp://127.0.0.1:7101");
```

이 방식은 테스트나 소규모 고정 토폴로지에 적합하다.

피어 엔드포인트를 모르고 대상 노드의 라우팅 ID만 알고 있으면
`zlink_spot_node_disconnect_peer_rid()`로 해당 피어 노드 연결을 종료할 수 있다.
이 함수는 `SpotNode`에 호출한다. `Spot` facade는 개별 피어 연결을 직접
소유하지 않으므로 별도의 라우팅 ID disconnect 함수를 제공하지 않는다.

### 3.2 raw peer weight로 새 outbound만 배제하기

SpotNode와 Spot에는 weight 설정 옵션이 없다. 서비스가 raw ROUTER 또는 worker
피어를 사용할 때 피어 연결은 유지한 채 새 routed/channel 요청만 잠시
빼고 싶으면 해당 raw 소켓의 weight를 `0`으로 설정한다. 값 범위는 `0..100`,
기본값은 `100`이다.

```c
int drain_weight = 0;
zlink_set_router_option(
  router,
  ZLINK_ROUTER_OPT_WEIGHT,
  &drain_weight,
  sizeof(drain_weight));

int serve_weight = 100;
zlink_set_router_option(
  router,
  ZLINK_ROUTER_OPT_WEIGHT,
  &serve_weight,
  sizeof(serve_weight));
```

weight가 `0`이면 다른 피어가 이 노드를 새 outbound 후보에서 제외한다. 기존
연결과 이미 진행 중인 request의 reply는 그대로 유지된다. 점검이 끝나면
양수 값으로 되돌리면 된다.

## 4. 토픽 publish/subscribe

SPOT 토픽 평면은 `topic_id`로 토픽을 구분한다. 토픽 네임스페이스(channel)는
`SpotNode`/소켓의 channel name으로 정해지고, publish/subscribe 인자에는 `topic_id`만 넘긴다.

### 4.1 publish

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 4);
memcpy(zlink_msg_data(&part), "tick", 4);

zlink_spot_publish(spot, "price.btcusd", &part, 1, 0);
zlink_msg_close(&part);
```

### 4.2 subscribe

```c
zlink_set_subscription(spot, "price.*");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char topic_id[256];
size_t topic_id_len = sizeof(topic_id);

int rc = zlink_spot_subscribe(
  spot,
  &source_rid,
  &parts,
  &part_count,
  topic_id,
  &topic_id_len,
  0);
```

성공하면 소스 라우팅 ID, 토픽 이름, multipart payload(메시지의 실제 데이터 내용)를 함께 받는다.

같은 노드 안에서 여러 `Spot`이 같은 토픽이나 접두사를 구독해도, 원격 피어에는
노드 단위 집계 구독으로 반영된다. 첫 구독이 생길 때 원격 구독이 등록되고
마지막 구독이 사라질 때 해제된다. 애플리케이션은 이 집계를 직접 관리하지 않아도 된다.

## 5. 다른 channel 호출

`Spot`에서 다른 채널의 서비스 처리자 집합으로 요청을 보내려면
`SpotNode`에 `DEALER`를 등록해야 한다.

핵심 규칙:

- 채널 호출은 항상 등록된 `DEALER`를 통해서만 나간다.
- 등록 함수는 소켓 생성이나 연결(connect)을 대신하지 않는다.

현재 공개 C API는 수동 연결 경로를 사용한다. 호출자가 `DEALER` 소켓을 만들고
알고 있는 endpoint에 `connect()`를 호출한 뒤 route bridge에 등록한다.

### 5.1 수동 연결 경로

고정 엔드포인트를 아는 경우에는 호출자가 `connect()`를 먼저 완료한 뒤 DEALER를 등록한다.

```c
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(dealer, "tcp://127.0.0.1:7201");
zlink_connect(dealer, "tcp://127.0.0.1:7202");

void *bridge = zlink_spot_route_bridge_new(ctx, node, NULL);
zlink_spot_route_bridge_attach_dealer_channel(bridge, "orders", dealer, NULL);
```

### 5.3 channel 호출

```c
zlink_msg_t req;
zlink_msg_init_size(&req, 5);
memcpy(zlink_msg_data(&req), "hello", 5);

zlink_spot_send_channel(spot, "orders", &req, 1, 0);

zlink_spot_request_channel(
  spot,
  "orders",
  &req,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

같은 `channel_name`에 `DEALER`를 두 개 등록할 수 없다.

## 6. dispatch 이벤트 핸들러로 통합 소비

`zlink_spot_dispatch_event_handler()`는 SPOT의 단일 readiness handler다. 구독, 라우팅, 채널 응답, 타이머, Actor 참가, Actor 읽기 준비, Actor lifecycle readiness를 알린다. callback은 "읽을 것이 있다"는 신호만 주며, payload와 lifecycle data는 각 drain API로 읽는다.

`zlink_spot_dispatch_event_handler()`를 등록하면 callback signature는 아래처럼
`event`뿐 아니라 `subject_kind`와 `subject`도 전달한다.

```c
void my_dispatch_handler(
  void *spot_,
  const zlink_spot_dispatch_info_t *info_,
  void *userdata_)
{
    switch (info_->event) {
    case ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE:
        /* zlink_spot_subscribe() 로 drain */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE:
        /* zlink_spot_recv() 로 drain */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE:
        /* subject가 attached dealer handle */
        zlink_spot_channel_reply_progress_from(spot_, info_->subject);
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE:
        /* subject가 timer handle */
        zlink_timer_recv(info_->subject, NULL);
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE:
        /* zlink_spot_actor_join_recv() 로 drain */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE:
        /* zlink_spot_recv_actor_lifecycle() 로 drain */
        break;
    case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE:
        /* subject가 const zlink_actor_ref_t* */
        break;
    }
}
```

dispatch 우선순위는 `SUBSCRIBE_READABLE` → `ROUTED_READABLE` →
`ACTOR_JOIN_READABLE` → `ACTOR_LIFECYCLE_READABLE` → `ACTOR_READABLE` →
`CHANNEL_REPLY_READABLE` → `TIMER_READABLE` 순이다. 모든 이벤트가 같은 콜백에서 처리되므로
하나의 Spot에서 라우팅 핸들러, 구독 핸들러, 타이머 핸들러, 채널 응답 콜백은
동일한 실행 문맥에서 순차적으로 실행된다.

### 6.1 dispatch 이벤트는 읽기 준비 신호다

`SUBSCRIBE_READABLE`과 `ROUTED_READABLE`은 "메시지 1개가 도착했다"는 뜻이 아니라
"지금 읽을 것이 있다"는 뜻이다.

따라서:

- 콜백 1회가 메시지 1개를 의미하지 않는다.
- 같은 평면(plane)이 이미 읽기 가능한 상태에서 메시지가 더 들어오면 콜백 횟수와 메시지
  개수가 1:1로 맞지 않을 수 있다.
- 콜백 안에서는 해당 평면을 `EAGAIN`이 나올 때까지 반복해서 소진(drain)해야 한다.

routed plane은 아래처럼 처리한다.

```c
for (;;) {
    const zlink_routing_id_t *source_rid = NULL;
    const zlink_routing_id_t *spot_rid = NULL;
    uint64_t request_seq = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;

    int rc = zlink_spot_recv(
      spot_,
      &source_rid,
      &spot_rid,
      &request_seq,
      &parts,
      &part_count,
      ZLINK_DONTWAIT);

    if (rc == ZLINK_RECV_NO_DATA && zlink_errno() == EAGAIN)
        break;
    if (rc != ZLINK_RECV_OK)
        break;

    /* parts 처리 */
    zlink_multipart_close(parts, part_count);
}
```

구독 평면도 같은 방식으로 소진(drain)한다.

### 6.2 채널 요청 응답이 dispatch 스트림에 포함되는 이유

`zlink_spot_request_channel()`로 시작한 요청의 응답은 전송 경로상으로는
연결된 `DEALER`를 통해 돌아오지만, **최종 콜백 실행은 해당 `Spot`의 dispatch
스트림에서 처리된다**.

- 네트워크 응답 → 연결된 `DEALER` 완료 → 브리지 → `Spot` dealer 소스 큐
- `CHANNEL_REPLY_READABLE` dispatch 이벤트 → `zlink_spot_channel_reply_progress_from()`
  → 사용자 응답 콜백

따라서 바인딩 계층이 연결된 dealer별로 별도 진행 펌프(progress pump)를 돌릴 필요가 없다.

## 7. Actor로 세션 메시지 분배하기

Actor 생성, Spot join/leave, 종료, STREAM session bind, C sample은
[SPOT Actor 가이드](07-4-actor.ko.md)를 본다.

## 8. 공개 poller와의 관계, Spot 타이머

현재 공개 poller는 `Spot` 전용 이벤트 종류와 주체(subject)를 함께 반환하지 않는다.
즉 `Spot`을 poller에 등록해서 dispatch 콜백과 같은 의미를 받는 인터페이스는
아직 없다.

SPOT의 구독, 라우팅 수신, 채널 응답, 타이머를 하나의 소유자 기준으로
순차 처리하려면 `zlink_spot_dispatch_event_handler()`를 사용해야 한다.
`Spot` 진행(progress) 하나만으로 채널 응답 완료를 포함한 모든 작업이 진전된다.

Spot의 I/O 스레드에서 실행되는 타이머가 필요하면 `zlink_timer_new()` 대신
`zlink_spot_timer_new()`를 사용한다:

```c
void *timer = zlink_spot_timer_new(spot);
zlink_timer_start(timer, 1000000000ULL, 0);  /* 1초 간격, 무한 반복 */
zlink_timer_handler(timer, my_timer_fn, userdata);
zlink_timer_destroy(&timer);
```

`zlink_spot_timer_new()`는 Spot 내부 I/O 컨텍스트에 타이머를 붙인다. 단,
`zlink_timer_handler()`로 등록한 콜백은 scheduler thread에서 직접 실행되므로 Spot
dispatch와 같은 직렬 컨텍스트가 아니다. Spot dispatch에서 처리하려면 handler 없이
timer readable(`ZLINK_POLLIN`)을 관찰한 뒤 `zlink_timer_recv()`로 drain한다.

## 9. 라우팅 수신과 응답

SPOT routed plane은 수신 시 source node 라우팅 ID, source spot 라우팅 ID, request sequence를
함께 반환한다.

```c
const zlink_routing_id_t *source_node_rid = NULL;
const zlink_routing_id_t *source_spot_rid = NULL;
uint64_t request_seq = 0;
zlink_msg_t *parts = NULL;
size_t part_count = 0;

int rc = zlink_spot_recv(
  spot,
  &source_node_rid,
  &source_spot_rid,
  &request_seq,
  &parts,
  &part_count,
  0);
```

`zlink_spot_recv()`의 출력값으로 어떤 응답 함수를 써야 하는지 판단한다.

- `source_spot_rid`가 비어 있지 않으면 다른 Spot에서 온 요청이다 —
  `zlink_spot_reply_spot()`으로 SPOT 라우팅 평면을 통해 응답한다.
- `source_spot_rid`가 비어 있고 `source_node_rid`만 있으면 ROUTER 소켓에서 온
  요청이다 — `zlink_spot_reply_router()`로 ROUTER 평면을 통해 응답한다.

잘못된 응답 함수를 사용하면 `ZLINK_SUBMIT_INVALID_ARGUMENT`가 반환된다.

## 10. Spot에서 라우팅 요청 시작하기

`Spot`은 라우팅 요청과 단방향 직접 전송을 직접 시작할 수 있다.
기본 경로는 `send_channel()` / `request_channel()`이지만 특정 피어를 직접
지목할 때는 아래 API를 사용한다.

### 10.1 다른 Spot으로 요청 보내기

```c
zlink_spot_request_spot(
  spot,
  &dest_node_rid,    /* 대상 SpotNode의 routing id */
  &dest_spot_rid,    /* 대상 Spot의 routing id */
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

reply는 대상 Spot이 `zlink_spot_reply_spot()`으로 보낸다.

### 10.2 Router 피어로 요청 보내기

```c
zlink_spot_request_router(
  spot,
  &peer_rid,         /* 대상 ROUTER peer의 routing id */
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

reply는 대상 ROUTER가 `zlink_router_reply_spot()`으로 보낸다.

### 10.3 Spot에서 Spot으로 단방향 직접 전송

`Spot`에서 라우팅 ID를 직접 지정해 다른 `Spot`으로 단방향 전송을 하려면
`zlink_spot_send_spot()` (C API) 또는 내부 기반 함수 `zlink_spot_send_spot_part()`를
사용한다.

ROUTER 피어로의 단방향 전송은 현재 공개 인터페이스에 없다. 필요하면 `RouterSocket`
또는 raw ROUTER API를 사용한다.

## 11. Router에서 SPOT으로 직접 보내기

특정 대상 노드 라우팅 ID와 Spot 라우팅 ID를 직접 지정해 ROUTER에서 SPOT으로
단방향 전송 또는 요청을 보낼 때는 `RouterSocket` 또는 raw ROUTER API를 사용한다.

```c
zlink_router_request_spot(
  router,
  &dest_node_rid,
  &dest_spot_rid,
  &part,
  1,
  my_reply_handler,
  my_userdata,
  0,
  2000);
```

## 12. 일반 PUB에서 SPOT으로 publish 넣기

외부 코드에서 SPOT 토픽 평면으로 발행을 넣고 싶다면 node에서 publisher handle을
만들어 publish한다.

```c
void *publisher = zlink_spot_node_publisher_new(node);
zlink_spot_node_publisher_publish(publisher, "orders", parts, part_count, 0);
```

외부 publisher가 끝나면 publisher handle을 닫는다.

## 13. 상태 확인

디버깅이나 운영 상태 확인에는 node snapshot과 query API를 사용한다.

```c
zlink_spot_node_status_t status;
zlink_spot_node_status(node, &status);

size_t peer_count = 0;
zlink_spot_node_peers(node, NULL, NULL, &peer_count);
```

더 자세한 상태 변화가 필요하면 연속된 snapshot/query 결과를 비교한다.
`status.disconnected_sub_target_count`와 `status.disconnected_routed_target_count`는
**ABI 호환 필드**로 항상 `0`을 보고한다. 현재 SPOT delivery 모델은 큐 증가를 이유로
delivery target을 끊지 않으므로 이 카운터는 진단에 사용하지 않는다.

**HWM 진단**: 입장 허용(admission, 큐 수용 여부 판단)은 `publish_ingress_queue`와
`routed_send_queue` 큐 한도로 적용된다.
`zlink_spot_node_internal_sockets()`으로 반환되는 `mesh-pub`,
`mesh-xsub`, `routed-router`의 `monitor_status` 필드는 transport 소켓 HWM을 보여준다.
relay 및 delivery 소켓은 HWM `0`을 보고하며 이는 정상이다.
큐 입장 허용 한도는 HWM 프로필 옵션으로 제어하며 프로필별 메시지 수 기준은
BALANCED 256 (기본), COMPACT 64, LOW_LATENCY 128, THROUGHPUT 512다.

SpotNode HWM(High Water Mark, 큐 상한선) 옵션은 입장 허용 경계에만 적용된다 — 토픽 발행 입장 허용과 라우팅 입장 허용이 해당된다. Actor 전용 HWM 옵션은 없다. Actor 처리 적체(backlog)는 dispatch 이벤트, 수신 결과, `zlink_spot_actors()`의 `unread` 카운트로 진단한다.

Actor 상태 확인에는 `zlink_spot_node_actors()`과
`zlink_spot_actors()`을 사용한다. 스냅샷의 unread count와 joined 상태는
운영 진단용이다. 메시지 처리나 흐름 제어 결정은 dispatch 이벤트와 recv
결과를 기준으로 한다.

라우팅 ID로 기존 `Spot` facade를 조회하려면:

```c
void *spot = NULL;
zlink_config_result_t rc = zlink_spot_node_spot_lookup(node, &spot_rid, &spot);
if (rc == ZLINK_CONFIG_OK) {
    /* spot 사용 */
    zlink_spot_destroy(&spot);  /* 사용 후 borrow된 facade를 닫는다 */
}
```

반환된 facade는 borrow 관계다. 사용 후 `zlink_spot_destroy()`로 닫는다.
기저 `SpotNode`는 영향받지 않는다.

## 14. Router channel에서 Spot으로 받기

일반 SPOT mesh 외에도, router 역할이 있는 channel의 `ROUTER`가 특정
`Spot`으로 메시지를 보낼 수 있다. framework에서는 같은 프로세스의 RouteMesh와
SpotMesh가 이 수신 경로로 자동 연결된다.

fanout channel과 dealer mesh channel은 router 역할이 없으므로 이 용도에 맞지 않는다.

보내는 쪽에서는 이미 연결된 local egress channel 을 골라야 한다. target Spot 이름이나
Spot rid 만으로 어떤 connection 을 사용할지 항상 알 수 없기 때문이다. 상위 framework
문서는 이 구분을 public API 로 드러내야 하며, local egress channel 이름과 target
SpotNode ingress channel 이름을 같은 값이라고 가정하면 안 된다.

core API를 직접 사용할 때는 caller가 소유한 channel socket을
`zlink_spot_route_bridge_*` handle에 등록한다. 내부 routed endpoint나 포트 파생 규칙은
application이 알 필요가 없다.

## 15. Actor C sample

[SPOT Actor 가이드](07-4-actor.ko.md#5-actor-c-sample)를 본다.

---
<!-- zlink-nav:bottom:start -->
[← 서비스](07-0-services.ko.md) | [SPOT Actor →](07-4-actor.ko.md)
<!-- zlink-nav:bottom:end -->
