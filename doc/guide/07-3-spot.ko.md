[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

# SPOT 사용 가이드

이 문서는 애플리케이션 개발자가 SPOT을 어떻게 쓰는지 설명한다.
정확한 함수 계약은 [SPOT spec](../spec/core/service/spot.ko.md)를 본다.

## 1. SPOT이 하는 일

SPOT은 `SpotNode`와 `Spot` 두 층으로 나뉜다.

- `SpotNode`
  노드 토폴로지와 discovery 기반 연결, 수동 peer 연결, channel 호출용
  `DEALER`, 외부 publish ingress를 관리한다.
- `Spot`
  애플리케이션이 실제로 쓰는 facade다. 토픽 publish/subscribe, routed recv,
  channel send/request를 제공한다.

보통 순서는 이렇다.

1. `SpotNode`를 만든다.
2. bind 또는 discovery attach로 node를 네트워크에 올린다.
3. 필요하면 channel 호출용 `DEALER`를 붙인다.
4. `Spot` facade를 만든다.
5. `Spot`으로 publish/subscribe 또는 channel 호출을 사용한다.

`zlink_spot_new()`가 성공하면 그 `Spot`의 routed recv 평면은 이미 준비된 상태다.
따라서 첫 `zlink_spot_recv()`가 숨은 activation이나 숨은 자원 생성을 수행한다고
가정하면 안 된다.

## 2. 가장 단순한 흐름

```c
void *ctx = zlink_ctx_new();
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:7001");

void *spot = zlink_spot_new(node);

zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "hello", 5);

zlink_spot_publish(spot, "market", "price.usdkrw", &msg, 1, 0);
zlink_msg_close(&msg);

zlink_spot_destroy(&spot);
zlink_spot_node_destroy(&node);
zlink_ctx_term(&ctx);
```

이 예제는 한 프로세스 안에서 SPOT node를 만들고, unified `Spot` facade로
토픽 하나를 발행하는 가장 작은 흐름이다.

## 3. Node를 네트워크에 올리는 방법

### 3.1 수동 peer 연결

고정된 endpoint를 알고 있으면 node끼리 직접 연결할 수 있다.

```c
void *a = zlink_spot_node_new(ctx);
void *b = zlink_spot_node_new(ctx);

zlink_spot_node_bind(a, "tcp://127.0.0.1:7101");
zlink_spot_node_bind(b, "tcp://127.0.0.1:7102");

zlink_spot_node_connect_peer(a, "tcp://127.0.0.1:7102");
zlink_spot_node_connect_peer(b, "tcp://127.0.0.1:7101");
```

이 방식은 테스트나 작은 고정 토폴로지에 적합하다.

### 3.2 discovery 기반 연결

운영 환경에서는 discovery를 붙여 SPOT mesh를 자동으로 구성하는 쪽이 보통 낫다.

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://127.0.0.1:0");

void *discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SPOT,
  "alpha");
zlink_discovery_connect_registry(discovery, "tcp://127.0.0.1:5551");

zlink_spot_node_attach_discovery(node, discovery);
```

여기서 `"alpha"`는 이 discovery view가 보는 SPOT channel 이름이다.
같은 channel view를 공유하는 다른 SPOT peer끼리 자동 연결된다.

`attach_discovery()`를 쓴 뒤에는 같은 node에 `connect_peer()`나
`disconnect_peer()`를 같이 섞지 않는 편이 좋다. 현재 계약도 discovery attach
후 수동 peer connect를 `EBUSY`로 막는다.

peer endpoint를 모르고 target node의 routing id만 알고 있으면
`zlink_spot_node_disconnect_peer_rid()`로 해당 peer node 연결을 종료할 수 있다.
이 함수는 `SpotNode`에 호출한다. `Spot` facade는 개별 peer 연결을 직접
소유하지 않으므로 별도 rid disconnect 함수를 제공하지 않는다.

### 3.3 raw peer weight로 새 outbound만 비우기

SpotNode와 Spot에는 weight 설정 옵션이 없다. 서비스가 raw ROUTER 또는 worker
DEALER peer를 사용할 때 peer 연결은 유지한 채 새 routed/channel 요청만 잠시
빼고 싶으면 해당 raw socket의 weight를 `0`으로 둔다. 값 범위는 `0..100`,
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

weight가 `0`이면 다른 peer가 이 node를 새 outbound 후보에서 제외한다. 기존
연결과 이미 진행 중인 request의 reply는 그대로 유지된다. maintenance가 끝나면
다시 양수 값으로 올리면 된다.

## 4. 토픽 publish/subscribe

SPOT topic plane은 `service_name + topic_id`를 함께 사용한다.
현재 공개 함수 인자 이름은 `service_name`이지만, 실질적으로는 topic namespace를
구분하는 이름으로 보면 된다.

### 4.1 publish

```c
zlink_msg_t part;
zlink_msg_init_size(&part, 4);
memcpy(zlink_msg_data(&part), "tick", 4);

zlink_spot_publish(spot, "market", "price.btcusd", &part, 1, 0);
zlink_msg_close(&part);
```

### 4.2 subscribe

```c
zlink_set_subscription(spot, "price.*");

zlink_routing_id_t source_rid;
zlink_msg_t *parts = NULL;
size_t part_count = 0;
char service_name[256];
size_t service_name_len = sizeof(service_name);
char topic_id[256];
size_t topic_id_len = sizeof(topic_id);

int rc = zlink_spot_subscribe(
  spot,
  &source_rid,
  &parts,
  &part_count,
  service_name,
  &service_name_len,
  topic_id,
  &topic_id_len,
  0);
```

성공하면 source routing id, topic 이름, multipart payload를 함께 받는다.

## 5. 다른 channel 호출

`Spot`에서 다른 channel의 서비스 처리자 집합으로 요청을 보내려면
`SpotNode`에 `DEALER`를 attach해야 한다.

핵심 규칙은 두 가지다.

- channel 호출은 항상 attach된 `DEALER`로만 나간다.
- attach 함수는 socket 생성이나 connect를 대신하지 않는다.

### 5.1 자동 연결 경로

이 방식은 discovery가 관리하는 `DEALER`를 node에 등록한다.

```c
void *node = zlink_spot_node_new(ctx);

void *spot_discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SPOT,
  "alpha");
zlink_discovery_connect_registry(spot_discovery, "tcp://127.0.0.1:5551");
zlink_spot_node_attach_discovery(node, spot_discovery);

void *orders_discovery = zlink_discovery_new(
  ctx,
  ZLINK_SERVICE_TYPE_SOCKET,
  "orders");
zlink_discovery_connect_registry(orders_discovery, "tcp://127.0.0.1:5551");

void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_socket_attach_discovery(dealer, orders_discovery);

zlink_spot_node_attach_channel_dealer(node, orders_discovery, dealer);
```

여기서는 `SpotNode` 자신이 보는 SPOT channel은 `"alpha"`이고,
attach하는 `DEALER`는 `"orders"` channel을 바라본다.
같은 이름을 써도 계약 위반은 아니지만, 예시에서는 헷갈리지 않게 다른 이름을
사용하는 편이 낫다.

### 5.2 수동 연결 경로

고정 endpoint를 아는 경우에는 호출자가 `connect()`를 먼저 끝낸 뒤 attach한다.

```c
void *dealer = zlink_socket(ctx, ZLINK_SOCKET_DEALER);
zlink_connect(dealer, "tcp://127.0.0.1:7201");
zlink_connect(dealer, "tcp://127.0.0.1:7202");

zlink_spot_node_attach_channel_dealer_manual(node, "orders", dealer);
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

같은 `channel_name`에 `DEALER`를 두 개 등록할 수는 없다. 자동 attach와 수동
attach도 같은 이름이면 충돌로 취급한다.

## 6. Dispatch event handler로 통합 소비

`zlink_spot_dispatch_event_handler()`를 등록하면 subscribe, routed, channel reply,
timer를 하나의 callback으로 받을 수 있다. callback signature는 아래처럼 `event`뿐
아니라 `subject_kind`와 `subject`도 전달한다.

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
        zlink_timer_recv(info_->subject, NULL, 0);
        break;
    }
}
```

dispatch 우선순위는 `SUBSCRIBE_READABLE` → `ROUTED_READABLE` →
`CHANNEL_REPLY_READABLE` → `TIMER_READABLE` 순이다. 같은 callback 안에서
모든 event를 처리하므로, 한 spot의 routed handler, subscription handler, timer
handler, channel reply callback은 같은 실행 문맥에서 순차 실행된다.

### 6.1 dispatch event는 readiness다

`SUBSCRIBE_READABLE`과 `ROUTED_READABLE`은 "메시지 1개가 도착했다"는 뜻이 아니라
"지금 읽을 것이 있다"는 뜻이다.

즉 아래처럼 이해해야 한다.

- callback 1회가 메시지 1개를 뜻하지 않는다.
- 같은 plane이 이미 readable인 동안 메시지가 더 들어와도 callback 개수는 메시지
  개수와 1:1로 맞지 않을 수 있다.
- callback 안에서는 해당 plane을 `EAGAIN`이 나올 때까지 반복해서 drain하는 편이
  맞다.

예를 들면 routed plane은 아래처럼 처리한다.

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

subscribe plane도 같은 방식으로 drain한다.

### 6.2 channel request reply가 dispatch stream에 포함되는 이유

`zlink_spot_request_channel()`로 시작한 request의 reply는 transport 경로상으로는
attach된 `DEALER`를 통해 돌아오지만, **최종 callback 실행은 해당 `Spot`의 dispatch
stream에서 처리된다**.

- network reply → attached `DEALER` completion → bridge → `Spot` dealer source queue
- `CHANNEL_REPLY_READABLE` dispatch event → `zlink_spot_channel_reply_progress_from()`
  → user reply callback

이 때문에 binding이 attached dealer별 별도 progress pump를 돌릴 필요가 없다.

## 7. 지금 공개된 poller와의 관계

현재 public poller는 `Spot` 전용 event kind와 subject를 함께 돌려주지 않는다.
즉 지금 공개 계약에서는 `Spot`을 poller에 등록해서 dispatch callback과 같은 의미를
그대로 받는 표면이 아직 없다.

따라서 SPOT의 subscribe, routed recv, channel reply, timer를 한 owner 기준으로
순차 처리하려면 현재는 `zlink_spot_dispatch_event_handler()`를 사용하는 쪽이 맞다.
`Spot` progress 하나만으로 channel reply completion을 포함한 모든 work가 진전된다.

## 8. Routed receive와 reply

SPOT routed plane은 수신 시 source node rid, source spot rid, request sequence를
함께 준다.

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

수신한 요청에 답할 때는 들어온 주소를 그대로 사용한다.

- 상대가 SPOT이면 `zlink_spot_reply_spot()`
- 상대가 ROUTER면 `zlink_spot_reply_router()`

## 9. Spot에서 routed request 시작하기

`Spot`은 routed request와 one-way direct send를 직접 시작할 수 있다.
기본 경로는 `send_channel()` / `request_channel()`이지만, 특정 peer를 직접
지목할 때는 아래 API를 사용한다.

### 9.1 다른 Spot으로 request 보내기

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

### 9.2 Router peer로 request 보내기

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

### 9.3 Spot에서 Spot으로 one-way direct send

`Spot`에서 `rid`를 직접 지정해 다른 `Spot`으로 one-way send를 하려면
`zlink_spot_send_spot()` (C API) 또는 내부 substrate `zlink_spot_send_spot_part()`를
사용한다.

ROUTER peer로 one-way send는 현재 공개 표면에 없다. 필요하면 `RouterSocket`
또는 raw ROUTER API를 쓴다.

## 10. Router에서 SPOT으로 직접 보내기

특정 destination node rid와 spot rid를 직접 지정해 ROUTER에서 SPOT으로
one-way send 또는 request를 보낼 때는 `RouterSocket` 또는 raw ROUTER API를 쓴다.

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

## 11. 일반 PUB에서 SPOT으로 publish 넣기

외부 일반 `PUB`에서 SPOT topic plane으로 publish를 넣고 싶다면 ingress용 `PUB`를
등록한다.

```c
void *pub = zlink_socket(ctx, ZLINK_SOCKET_PUB);
zlink_spot_node_attach_pub_ingress(node, pub);
```

이 `PUB`는 `SpotNode` 전용 ingress source로 취급한다. node당 하나만 붙일 수 있고,
attach 뒤에는 다른 일반 용도로 함께 쓰지 않는 편이 맞다.

## 12. 상태 확인

디버깅이나 운영 상태 확인에는 node snapshot과 service monitor를 사용한다.

```c
zlink_spot_node_status_t status;
zlink_spot_node_status_snapshot(node, &status);

size_t peer_count = 0;
zlink_spot_node_peers_snapshot(node, NULL, &peer_count);
```

좀 더 자세한 상태 이벤트가 필요하면 `zlink_service_monitor_open()`으로 monitor를
열고 `zlink_service_monitor_recv()` 또는 handler를 사용한다.
