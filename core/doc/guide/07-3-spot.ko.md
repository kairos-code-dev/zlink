[English](07-3-spot.md) | [한국어](07-3-spot.ko.md)

<!-- zlink-nav:start -->
[← 서비스](07-0-services.ko.md) | [SPOT Actor →](07-4-actor.ko.md)
<!-- zlink-nav:end -->

# SPOT 사용 가이드

이 문서는 애플리케이션 개발자가 10.1.0 MeshNode 위에서 Spot을 어떻게 쓰는지
설명한다. 정확한 함수 계약은
[MeshNode spec](../spec/core/service/01-mesh-node.ko.md),
[Dispatch spec](../spec/core/service/02-dispatch.ko.md),
[Spot spec](../spec/core/service/03-spot.ko.md)이 소유한다.

> Spot을 **언제·왜** 쓰는지(raw 소켓·Actor와의 관계, 실행 직렬성)는
> [서비스 개요 §멘탈 모델](07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)에서
> 먼저 잡고 오면 이 문서의 how-to가 더 잘 읽힌다.

## 1. 구조: MeshNode 하나, Spot 여럿

10.1.0의 수명·transport 소유자는 **MeshNode** 하나다.

- `MeshNode` — MeshName 하나, ROUTER bind 하나, 프로세스당 유일. peer
  admission, channel 라우팅, Logical Multicast, dispatch와 monitor를 소유한다.
- `Spot` — MeshNode 안의 논리 상태 단위다. RID로 식별하고, channel 구독,
  direct 메시징, publish, timer를 제공한다. `Spot` 핸들은 얇은 facade라서 같은
  논리 Spot을 여러 핸들이 가리킬 수 있다.

일반적인 순서:

1. `zlink_mesh_node_new()`로 node를 만든다 (MeshName 필수).
2. routing id, bind endpoint, 참여할 ChannelName을 설정한다.
3. `zlink_mesh_node_start()`로 기동한다.
4. `zlink_mesh_node_connect_peer()`로 다른 node에 연결한다.
5. `zlink_mesh_node_spot_get_or_new()`로 Spot을 확보하고 구독·메시징한다.
6. ready/claim/receive batch로 메시지를 소비한다.

## 2. 가장 단순한 흐름

```c
void *ctx = zlink_ctx_new();

zlink_mesh_node_options_t opts;
memset(&opts, 0, sizeof(opts));
opts.struct_size = sizeof(opts);
opts.version = 1;
opts.mesh_name = "market-mesh";
opts.mesh_name_size = strlen("market-mesh");
void *node = zlink_mesh_node_new(ctx, &opts);

zlink_set_routing_id(node, "md-seoul-1", 10);
zlink_mesh_node_set_bind(node, "tcp://10.20.8.11:7001");
zlink_mesh_node_add_channel_name(node, "md.krw");
zlink_mesh_node_start(node);

/* 구독자 Spot: md.krw channel에서 price. prefix를 받는다. */
zlink_routing_id_t rid = {0};
rid.size = 9;
memcpy(rid.data, "book-krw1", 9);
void *spot = NULL;
uint32_t created = 0;
zlink_mesh_node_spot_get_or_new(node, &rid, &spot, &created);
zlink_spot_set_subscription(spot, "md.krw", "price.",
                            ZLINK_SPOT_SUBSCRIPTION_PREFIX);

/* 발행: node publisher가 channel 대상 Logical Multicast를 수행한다. */
void *pub = zlink_mesh_node_publisher_new(node);
zlink_msg_t msg;
zlink_msg_init_size(&msg, 12);
memcpy(zlink_msg_data(&msg), "1385.42,+0.3", 12);
zlink_mesh_node_publisher_publish(pub, "md.krw", "price.usdkrw",
                                  NULL, &msg, 1, NULL, 0);
zlink_msg_close(&msg);

/* ...소비는 §5의 claim 흐름으로... */

zlink_mesh_node_publisher_destroy(&pub);
zlink_spot_destroy(&spot);
zlink_mesh_node_shutdown(node, 5000);
zlink_mesh_node_destroy(&node);
zlink_ctx_term(ctx);
```

같은 프로세스에 같은 MeshName을 두 번 만들면 `EEXIST`다. start는 routing id,
bind, channel이 모두 설정된 뒤에만 성공하고, start 뒤 ChannelName 추가·제거는
`EBUSY`다(weight는 실행 중 변경 가능).

### 2.1 room id가 정해진 Spot 확보

게임 방이나 작업 그룹처럼 애플리케이션이 이미 room id를 알고 있는 경우에는
`zlink_mesh_node_spot_get_or_new()`를 사용한다. "있으면 가져오고 없으면
만든다"를 node 안에서 원자적으로 처리한다.

```c
zlink_routing_id_t room_rid = {0};
room_rid.size = 8;
memcpy(room_rid.data, "room-001", 8);

void *room = NULL;
uint32_t created = 0;
zlink_config_result_t rc =
  zlink_mesh_node_spot_get_or_new(node, &room_rid, &room, &created);

if (rc == ZLINK_CONFIG_OK && created) {
  /* 최초 생성자만 방 초기 상태를 구성한다. */
}
```

반환된 `room` facade는 `zlink_spot_destroy()`로 닫는다(논리 Spot 자체는 node가
소유한다). Actor를 방에 참가시키는 작업은 이 함수가 하지 않는다 — Spot 확보와
Actor join을 분리해야 "방이 없어서 만들었다"와 "방에는 도달했지만 참가가
거절되었다"를 다르게 처리할 수 있다. 조회만 하려면
`zlink_mesh_node_spot_lookup()`을 쓴다(`ENOENT`). node마다 자동 생성되는 entry
Spot은 `zlink_mesh_node_entry_spot()`으로 얻는다.

## 3. Node를 mesh에 올리는 방법

운영자가 모든 peer 연결을 설정한다. 같은 MeshName의 node끼리만 admission이
성립한다.

```c
zlink_mesh_peer_connection_options_t peer = {0};
peer.struct_size = sizeof(peer);
peer.version = 1;
peer.endpoint = "tcp://10.20.8.12:7001";
peer.endpoint_size = strlen(peer.endpoint);
uint64_t intent_id = 0;
zlink_mesh_node_connect_peer(node, &peer, &intent_id);
```

- admission handshake가 MeshName·RID·lifecycle generation·trust profile을
  검증한다. MeshName 불일치는 `EEXIST`, expected RID 불일치는 `ESTALE`,
  trust/authentication 실패는 `EACCES`로 관측된다.
- 아직 admission되지 않은 intent는 `zlink_mesh_node_remove_peer_connection()`
  (intent id)으로, admitted peer는 `zlink_mesh_node_disconnect_peer()`
  (RID+generation)로 정리한다.
- 상태 관찰: `zlink_mesh_node_status()`(node 상태·peer 수·pending 수),
  `zlink_mesh_node_peers()` / `zlink_mesh_node_peer_channels()`(peer snapshot).

## 4. 메시지를 보내는 세 가지 방법

| 방법 | API | 대상 결정 |
|---|---|---|
| channel 호출 | `zlink_spot_send_to_channel` / `zlink_spot_request_to_channel` (node 계열은 `zlink_mesh_node_*`) | 호출 시점 ready member 중 양수-weight round-robin 1곳 |
| Spot direct | `zlink_spot_send_to_spot` / `zlink_spot_request_to_spot` | (target node rid, target spot rid, generation)으로 지정한 Spot |
| Logical Multicast | `zlink_spot_publish` / `zlink_mesh_node_publisher_publish` | target channel의 member node 전체 + 각 node의 local 구독 match |

```c
/* channel request: 5초 timeout, completion은 소유 owner의 infra lane으로. */
zlink_mesh_operation_id_t op;
zlink_spot_request_to_channel(spot, "md.krw", NULL, &req, 1, &op, 0, 5000);

/* direct request: framework location 계층에서 조회한 주소의 특정 Spot에게.
   generation 0은 EINVAL — 주소는 언제나 generation까지 포함해야 한다. */
zlink_spot_request_to_spot(spot, &target_node_rid, &target_spot_rid,
                           target_spot_generation, NULL, &req, 1, &op, 0, 5000);
```

- request가 admission되면 non-zero operation ID가 나오고, terminal completion이
  requester owner의 infrastructure claim으로 정확히 한 번 돌아온다(원격 부재는
  `ESTALE`/`ENOENT` completion).
- application metadata는 선택적 `zlink_mesh_metadata_view_t`로 붙인다(canonical
  frame, 1024 byte 상한). reply에는 metadata를 붙이지 않는다.
- publish는 각 local mailbox와 remote ROUTER target에 독립적으로 제출한다.
  remote target의 HWM·timeout·backpressure는 ROUTER 송신 규칙을 따르며, 앞에서
  성공한 제출은 뒤 target의 실패 때문에 취소되지 않는다. 대상별 집계는
  `zlink_mesh_publish_detail_t`로 확인한다.

## 5. 메시지 소비: ready → claim → receive batch

수신은 폴링 대신 ready index를 통해 일어난다. Spot마다 application /
infrastructure 두 lane이 있고, 한 lane은 한 번에 하나의 claim만 허용한다 —
이것이 lock 없는 직렬 처리의 근거다.

```c
void *ready = zlink_mesh_ready_batch_new(16);
void *batch = zlink_mesh_receive_batch_new(16, 64, 1 << 20);

uint32_t residue = 0;
zlink_recv_result_t rc = zlink_mesh_node_drain_ready(
  node, ZLINK_MESH_READY_APPLICATION, ready, &residue, 0);
if (rc == ZLINK_RECV_OK) {
  size_t n = zlink_mesh_ready_batch_count(ready);
  const zlink_mesh_ready_record_t *rr = zlink_mesh_ready_batch_data(ready);
  for (size_t i = 0; i < n; ++i) {
    zlink_mesh_claim_t claim;
    if (zlink_mesh_ready_batch_take_claim(ready, i, &claim) != ZLINK_CONFIG_OK)
      continue;                      /* 다른 스레드가 먼저 가져감 */
    zlink_mesh_receive_requirements_t need = {0};
    need.struct_size = sizeof(need);
    need.version = 1;
    while (zlink_mesh_claim_recv_batch(&claim, batch, &need,
                                       ZLINK_RECV_FLAGS_DONTWAIT)
           == ZLINK_RECV_OK) {
      const zlink_mesh_receive_record_t *rec = zlink_mesh_receive_batch_data(batch);
      size_t records = zlink_mesh_receive_batch_count(batch);
      for (size_t r = 0; r < records; ++r) {
        switch (rec[r].kind) {
        case ZLINK_MESH_RECORD_SPOT_MULTICAST:
          /* rec[r].channel_name, rec[r].topic, parts */
          break;
        case ZLINK_MESH_RECORD_SPOT_REQUEST:
          zlink_mesh_reply(&rec[r].reply_token, reply_parts, 1, 0);
          break;
        default:
          break;
        }
      }
      zlink_mesh_receive_batch_reset(batch);
    }
    zlink_mesh_claim_release(&claim);   /* 남은 work가 있으면 자동 재무장 */
  }
}
```

핵심 규칙:

- record의 payload part는 batch가 소유하고 `reset`/`destroy`에서 닫힌다. 더
  오래 쓰려면 `zlink_mesh_receive_batch_retain_message()`로 참조를 가져간다.
- batch 용량이 부족하면 `ZLINK_RECV_BUFFER_TOO_SMALL`과 함께 필요한 크기가
  requirements로 돌아온다.
- reply token은 one-shot이다. 두 번째 reply는 `EALREADY`, 낡은 generation은
  `ESTALE`, node가 stop된 뒤에는 `ESHUTDOWN`.
- wakeup만 필요하면 `zlink_mesh_node_set_ready_handler()`(콜백에서 곧바로
  drain하지 말고 소비 스레드를 깨운다), 이벤트 루프 통합은 poller에 node를
  등록한다(`ZLINK_POLLER_SOURCE_MESH_NODE`, [polling spec](../spec/core/06-polling.ko.md)).
  handler와 poller 등록은 상호 배타다.

## 6. 구독 관리

```c
zlink_spot_set_subscription(spot, "md.krw", "price.usdkrw",
                            ZLINK_SPOT_SUBSCRIPTION_EXACT);
zlink_spot_set_subscription(spot, "md.krw", "price.",
                            ZLINK_SPOT_SUBSCRIPTION_PREFIX);
zlink_spot_unset_subscription(spot, "md.krw", "price.",
                              ZLINK_SPOT_SUBSCRIPTION_PREFIX);
```

- 구독은 channel-scoped local 상태다. 원격으로 전파되지 않는다 — multicast는
  node 단위로 도착하고 local match만 fan-out된다.
- 같은 구독의 중복 등록은 idempotent하고, 등록·해제는 진행 중인 publish와
  원자적으로 교체된다.
- 구독 inventory query는 제공하지 않는다. 필요한 구독 상태는 응용이 소유한다.

## 7. Spot timer

```c
void *timer = zlink_spot_timer_new(spot);
zlink_timer_start(timer, 250ull * 1000 * 1000 /* 250ms */, 0 /* repeat forever */);
/* ...tick 소비는 zlink_timer_recv() 또는 poller 등록으로... */
zlink_timer_stop(timer);
zlink_timer_destroy(&timer);
```

tick은 해당 Spot의 dispatch 흐름과 상호 배제되어 전달되고, Spot generation이
끝나면(파괴/이동) 더 이상 전달되지 않는다. 세부 계약은
[Spot spec §9](../spec/core/service/03-spot.ko.md)와
[utilities spec](../spec/core/08-utilities.ko.md)을 본다.

## 8. 자주 틀리는 부분

- **모든 핸들은 node보다 먼저 닫는다.** Spot facade·publisher·monitor·timer가
  살아 있으면 `zlink_mesh_node_destroy()`가 `EBUSY`로 거부한다.
- **claim을 잡은 채 같은 lane을 다시 기다리지 않는다.** 같은 owner의 다음
  turn은 release 뒤에 온다. 반대로 infrastructure lane은 application claim을
  잡은 동안에도 독립적으로 전진하므로, turn 안에서 completion을 기다리는
  in-turn await는 안전하다.
- **backpressure를 설계에 넣는다.** mailbox budget이 차면 submit이
  `ZLINK_SUBMIT_BACKPRESSURED`(`EAGAIN`)로 거부된다. 차단 submit은 SNDTIMEO까지
  기다린 뒤 `ETIMEDOUT`이다. Logical Multicast의 remote ROUTER 제출은 기존 ROUTER와 같이
  timeout 뒤에도 `EAGAIN`이다. monitor의 `BACKPRESSURED` event로 관측한다.
- **주소는 framework가 준다.** Core는 Spot 위치를 조회해 주지 않는다. 분산
  주소(`zlink_spot_address_t`)의 발급·조회는 framework location 계층의 책임이다.

---
<!-- zlink-nav:bottom:start -->
[← 서비스](07-0-services.ko.md) | [SPOT Actor →](07-4-actor.ko.md)
<!-- zlink-nav:bottom:end -->
