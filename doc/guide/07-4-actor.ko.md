[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

# SPOT Actor 사용 가이드

이 문서는 Actor 생성, Spot join/leave, 종료, session bind 흐름을 설명한다.
SPOT 기본 setup과 dispatch handler 등록은 [SPOT 가이드](07-3-spot.ko.md)를 본다.
정확한 함수 계약은 [SPOT spec](../spec/core/service/spot.ko.md)를 본다.

## 1. Actor로 session 메시지 분배하기

Actor는 session 메시지를 특정 처리 단위로 모으고, Spot dispatch callback에서
읽을 대상을 구분하고 싶을 때 쓴다. 한 session은 여러 Actor를 bind할 수 있고,
한 Actor는 동시에 하나의 session에만 bind된다.

Actor는 생성 직후 `Entry Spot`에 속한다. `Entry Spot`은 `SpotNode`가 항상 가지고
있는 기본 Spot이다. `Entry Spot`에 dispatch handler를 등록하면 새 Actor의 초기
메시지를 받아 인증하거나 대상 Spot을 선택할 수 있다.

Entry Spot facade는 아래처럼 얻는다.

```c
void *entry = NULL;
zlink_spot_node_entry_spot(node, &entry);
zlink_spot_dispatch_event_handler(entry, my_dispatch_handler, userdata);
```

Entry Spot facade를 다 쓴 뒤에는 `zlink_spot_destroy(&entry)`로 닫는다.
Entry Spot 자체는 `SpotNode`가 소유하므로 facade를 닫아도 Entry Spot은 사라지지 않는다.

가장 작은 흐름은 아래와 같다.

1. `SpotNode`에서 Actor를 만든다.
2. STREAM client session routing id를 확인한다.
3. `zlink_stream_bind_actor()`로 session과 Actor를 연결한다.
4. STREAM packet handler나 app 로직에서 `zlink_stream_send_bound_actor_part()`를
   호출해 Actor id를 선택한다.
5. dispatch callback에서 `ACTOR_READABLE`을 받으면 `subject` Actor ref를 복사하고
   `zlink_spot_node_actor_recv_part()`로 비운다.

```c
zlink_actor_ref_t ref;
zlink_spot_node_actor_new(node, "player-42", &ref);

zlink_stream_bind_actor(node, stream, &session_rid, &ref, 2000);

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_stream_send_bound_actor_part(
  node,
  stream,
  &session_rid,
  "player-42",
  &part,
  0,
  ZLINK_PART_FINAL);
```

Actor가 읽을 수 있게 되면 dispatch callback은 drain 대상 Actor ref를 알려준다.

```c
case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE: {
    const zlink_actor_ref_t *subject_ref =
      (const zlink_actor_ref_t *) info_->subject;
    zlink_actor_ref_t actor = *subject_ref;
    for (;;) {
        zlink_actor_recv_info_t recv_info;
        zlink_msg_t part;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        zlink_recv_result_t rc = zlink_spot_node_actor_recv_part(
          node,
          &actor,
          &recv_info,
          &part,
          &more,
          ZLINK_DONTWAIT);

        if (rc == ZLINK_RECV_NO_DATA)
            break;
        if (rc != ZLINK_RECV_OK)
            break;

        /* part 처리 */
        zlink_msg_close(&part);
    }
    break;
}
```

Actor 주소를 다른 node에서 알고 있어야 하면 Actor owner Discovery에서
`ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`를 켜고, Actor를 STREAM session에 bind한 뒤
`zlink_discovery_resolve_actor()`로 조회한다. Actor 생성이나 Spot join만으로는
active route가 공개되지 않는다. **active route는 STREAM session bind 성공 시점에만
publish된다.**

로컬 node에서 id로 기존 Actor를 조회하려면:

```c
zlink_actor_ref_t ref;
zlink_config_result_t rc = zlink_spot_node_actor_lookup(node, "player-42", &ref);
if (rc == ZLINK_CONFIG_OK) {
    /* Actor 존재 — ref 사용 가능 */
} else if (rc == ZLINK_CONFIG_NOT_FOUND) {
    /* 해당 id의 Actor 없음 */
}
```

Remote Actor가 필요하면 caller node에서 `zlink_spot_node_create_remote_actor()`를
사용한다. 이미 target node에 같은 Actor id가 있으면 새로 만들지 않고 existing
결과를 받는다. target node가 admission handler에서 reject하면 request는 거부
결과로 끝난다. remote create-or-get은 target Spot join handler를 거치지 않는다.
새로 만들어진 remote Actor도 target node의 Entry Spot에 속한다.

Remote Actor 생성 허용 여부를 제어하려면 `SpotNode`에 admission handler를 등록한다.
handler는 이 node로 향하는 모든 `zlink_spot_node_create_remote_actor()` 요청에 대해
동기적으로 실행된다:

```c
zlink_actor_admission_result_t my_admission(
  void *node_,
  const char *actor_id_,
  const zlink_msg_t *message_,
  void *userdata_)
{
    /* actor_id와 선택적 payload message를 검사 */
    if (/* 거부 조건 */)
        return ZLINK_ACTOR_ADMISSION_REJECT;
    return ZLINK_ACTOR_ADMISSION_ACCEPT;
}

zlink_spot_node_actor_admission_handler(node, my_admission, userdata);
```

`NULL`을 handler로 전달하면 이전에 등록된 handler를 제거한다 (기본 동작은
모든 remote create 요청 허용).

## 2. Spot join

Actor를 user Spot으로 보내려면 `zlink_spot_node_actor_join_spot()`으로 join request를
보낸다. target Spot은 `ACTOR_JOIN_READABLE` dispatch event를 받고,
`zlink_spot_actor_join_recv()`로 요청 message를 읽은 뒤 `zlink_spot_actor_join_reply()`로
accept 또는 reject를 보낸다.

```c
/* join request 보내기 */
zlink_spot_node_actor_join_spot(
  node, &actor_ref,
  &dest_node_rid, &dest_spot_rid,
  &payload_msg,       /* join state payload — NULL이면 빈 payload */
  my_join_handler,    /* completion callback */
  userdata,
  0, 3000);           /* flags, timeout_ms */

/* target Spot dispatch callback에서 */
case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE: {
    zlink_actor_join_info_t info;
    zlink_msg_t msg;
    while (zlink_spot_actor_join_recv(spot_, &info, &msg, ZLINK_DONTWAIT)
           == ZLINK_RECV_OK) {
        /* info.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE 이면 remote join */
        int accept = /* payload 검사 결과 */;
        zlink_spot_actor_join_reply(spot_, &info, accept, NULL);
        zlink_msg_close(&msg);
    }
    break;
}
```

join pending 중 새 join, leave, destroy는 busy 계열로 실패한다. Entry Spot이 아닌
user Spot으로 join하려면 source Actor에 bound STREAM session이 있어야 한다.
`timeout_ms`는 operation timeout이며, submit 단계의 비차단 여부는 `ZLINK_DONTWAIT`
flag로 제어한다.

## 3. Spot leave

`leave`는 Actor를 현재 Spot에서 Entry Spot으로 돌려보내는 동작이다. Actor가 이미
Entry Spot에 있으면 idempotent success다. leave 뒤 Actor message는 Entry Spot
dispatch event로 올라간다.

```c
/* game Spot에서 Entry Spot으로 복귀 */
zlink_spot_node_actor_leave_spot(
  node, &actor_ref,
  &current_spot_rid,  /* Actor의 현재 Spot rid */
  2000);
```

`current_spot_rid`는 stale leave를 막기 위한 optimistic check다. caller가 알고 있는
current Spot과 실제 current Spot이 다르면 invalid-state 계열로 실패한다. join pending
중에는 leave가 `EBUSY`로 실패하며, leave가 pending join을 취소하지는 않는다.

## 4. Actor 종료

Actor destroy는 Actor가 Entry Spot에 있을 때만 허용된다. user Spot에 있으면 먼저
`leave`로 Entry Spot으로 돌려보내야 한다.

```c
/* 먼저 leave */
zlink_spot_node_actor_leave_spot(node, &ref, &current_spot_rid, 2000);
/* Entry Spot 복귀 확인 뒤 destroy */
zlink_spot_node_actor_destroy(node, &ref, 2000);
```

bound STREAM session이 있는 상태에서 destroy하면 session Actor list 항목과 bound
session ref가 먼저 정리된다. client connection 자체는 닫히지 않는다. connection까지
함께 닫으려면 destroy 전에 `zlink_spot_node_actor_close_bound_session()`을 호출한다.

recv가 트리거 없이 STREAM client에 메시지를 push하려면
`zlink_spot_node_actor_send_bound_session_msg()`를 사용한다. Actor에 active bound
session이 있어야 하며, 없으면 호출이 실패한다.

```c
zlink_msg_t msg;
zlink_msg_init_size(&msg, 5);
memcpy(zlink_msg_data(&msg), "push!", 5);
zlink_spot_node_actor_send_bound_session_msg(node, &ref, &msg, 0);
```

## 5. Actor C sample

C 샘플에는 Actor 흐름을 나누어 보여 주는 세 파일이 있다.

| 흐름 | 파일 |
|---|---|
| 방 단위 Actor dispatch | `bindings/c/samples/actor_room_server_sample.c` |
| gateway session에서 remote Actor로 relay | `bindings/c/samples/actor_gateway_relay_sample.c` |
| 단일 사용자 queue 직렬화 | `bindings/c/samples/actor_single_player_queue_sample.c` |
