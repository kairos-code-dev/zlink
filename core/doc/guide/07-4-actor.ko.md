[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

<!-- zlink-nav:start -->
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
<!-- zlink-nav:end -->

# SPOT Actor 사용 가이드

이 문서는 Actor 생성, Spot join/leave, 메시징, 종료, STREAM 세션 바인딩과
transfer 흐름을 설명한다. MeshNode 기본 설정과 claim 소비 흐름은
[SPOT 가이드](07-3-spot.ko.md)를 본다. 정확한 함수 계약은
[Actor spec](../spec/core/service/04-actor.ko.md)과
[STREAM session spec](../spec/core/service/05-stream-session.ko.md)이 소유한다.

> Actor가 **무슨 역할이고 언제** 쓰는지(세션↔처리 단위 binding, 재접속 이전성,
> plain Spot과의 차이)는
> [서비스 개요 §멘탈 모델](07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)을
> 먼저 본다.

## 1. Actor 만들기와 entry Spot

Actor는 `zlink_actor_ref_t`(actor id + generation)로 식별하는 주소 지정
단위다. 소켓이나 프로세스 내부 endpoint를 소유하지 않고, 언제나 어떤 Spot에
join해 있다. 생성 직후에는 그 node의 entry Spot에 속한다.

```c
zlink_actor_ref_t player;
zlink_request_result_t rc = zlink_mesh_node_actor_new(
  node, "player-9421", NULL, 0, &player, 0, 2000);
/* 같은 id 재생성은 ZLINK_REQUEST_CONFLICT (EEXIST). */
```

- 생성은 entry Spot의 `SPOT_CONTROL` lane에 CREATED record를 남긴다. entry
  Spot의 claim 소비자가 이 record로 초기 인증·라우팅을 수행한다.
- 위치 조회는 local은 `zlink_mesh_node_actor_lookup()`(`ENOENT`), 원격 node에는
  `zlink_mesh_node_actor_lookup_remote()`(completion으로 location 반환)다.
  mesh 전체를 검색하는 API는 없다 — 분산 위치는 framework location 계층의
  책임이다.

## 2. Actor 메시징

```c
/* node → actor */
zlink_mesh_node_send_to_actor(node, &player, NULL, &part, 1, 0);
zlink_mesh_node_request_to_actor(node, &player, NULL, &part, 1, &op, 0, 3000);

/* actor → actor (source를 밝힌 호출) */
zlink_actor_request_to_actor(node, &player, &dealer, NULL, &part, 1, &op, 0, 3000);
```

- 메시지는 Actor mailbox로 직접 들어가고, 그 Actor owner의 application
  claim에서 `ACTOR_SEND`/`ACTOR_REQUEST` record로 나온다. FIFO는 owner 단위로
  보존된다.
- 낡은 generation의 ref는 `ESTALE`, 없는 Actor는 `ENOENT` completion이다.
- request completion은 호출 주체(Node 또는 source Actor) owner의
  infrastructure lane으로 돌아온다.

## 3. Spot join / leave

```c
/* 다른 node의 방으로 참가 요청 (원격이면 wire로 전달된다) */
zlink_mesh_node_actor_join_spot(node, &player, &room_node_rid, &room_spot_rid,
                                room_spot_generation, NULL, 0, &op, 3000);
```

join은 admission 절차다.

1. 요청이 target Spot의 `SPOT_CONTROL` lane에 JOIN record로 도착한다.
2. target 소비자가 `zlink_actor_join_reply(&record->reply_token,
   ZLINK_ACTOR_JOIN_ACCEPTED, ...)`로 판정한다. 이 token은 join 전용이라
   `zlink_mesh_reply()`에 넣으면 `EINVAL`이다.
3. ACCEPTED만 membership을 commit한다(epoch+1). 요청자는 completion으로
   결과·새 epoch를 받는다.

`zlink_mesh_node_actor_leave_spot()`은 expected epoch CAS로 entry Spot 복귀를
수행하고, `zlink_mesh_node_actor_join_entry_spot()`은 지정 node의 entry Spot으로
보낸다. `zlink_mesh_node_actor_destroy()`는 mailbox를 drain한 뒤 terminal
completion으로 끝난다.

## 4. STREAM 세션 바인딩

외부 byte 세션(게임 클라이언트 등)은 raw STREAM socket으로 들어온다. session과
Actor의 연결은 STREAM session service가 소유한다.

```c
void *svc = zlink_stream_session_service_new(stream_socket, node);
zlink_stream_session_service_start(svc);

/* 세션 라우팅 ID ↔ Actor binding (generation CAS, idempotent) */
zlink_mesh_operation_id_t bind_op;
zlink_stream_session_bind_actor(svc, &session_rid, &player, &bind_op, 2000);

/* 세션 byte → Actor로 relay */
zlink_stream_session_send_to_actor(svc, &session_rid, &player, NULL, &part, 1, 0);

/* Actor 쪽에서 세션으로 회신 */
zlink_mesh_node_actor_send_bound_session(node, &player, &part, 1, 0);
```

- 하나의 세션은 여러 Actor를 바인딩할 수 있고, binding CAS는 generation을
  검증한다.
- 세션 disconnect는 그 세션의 binding만 제거하며 Actor의 joined Spot은 바뀌지
  않는다 — 재접속한 새 세션을 같은 Actor에 다시 바인딩하면 이어진다(재접속
  이전성).
- `zlink_mesh_node_actor_close_bound_session()`은 Actor에 붙은 세션을 닫는다.

## 5. Actor transfer (이동)

Actor를 다른 node로 옮기는 data plane은 Core가 소유하고, 이동 결정과 분산
잠금(lease·participant CAS)은 framework transfer authority가 소유한다.

```
source node                                target node
  prepare  ──token──▶ (framework가 결정·조율)
  │ fence: 새 app 메시지 EAGAIN              prepare(placeholder, staged)
  │ frozen mailbox 재전송 ──TRANSFER_DATA──▶ staged 적재 (ACK)
  commit(token, epoch+1)                     activate: staged→mailbox, 해제
  actors 제거(잔여 infra drain 가능)
```

- `zlink_mesh_node_actor_transfer_prepare()`가 64-byte sealed token을 발급하고
  해당 Actor의 application lane을 fence한다(submit `EAGAIN`, claim `EBUSY`).
- `..._transfer_commit()`은 token·transfer ID·generation·정확히 다음 epoch를
  검증한다. 낡은 token은 `ESTALE`, 중복 commit은 `EALREADY`.
- `..._transfer_activate()`가 target에서 staged record를 mailbox로 옮기고
  fence를 푼다. `..._transfer_abort()`는 어느 단계에서든 원상 복구한다.
- 전송 중 도착한 request의 reply는 Core가 relay로 원 요청자에게 이어 준다.

## 6. 자주 틀리는 부분

- **entry Spot 소비자를 먼저 세운다.** CREATED/JOIN record를 아무도 claim하지
  않으면 생성·참가 흐름이 completion timeout으로 끝난다.
- **join token과 generic token을 섞지 않는다.** JOIN record의 token은
  `zlink_actor_join_reply()` 전용이다.
- **ref는 generation까지 저장한다.** Actor를 재생성하면 generation이 바뀌고,
  낡은 ref의 호출은 `ESTALE`로 끝난다.
- **transfer는 framework API를 통해 쓴다.** Core transfer API는 framework
  authority가 조율하는 저수준 fence 프로토콜이다. 응용에서 직접 호출할 일은
  거의 없다.

---
<!-- zlink-nav:bottom:start -->
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
<!-- zlink-nav:bottom:end -->
