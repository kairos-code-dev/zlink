[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

# SPOT Actor 사용 가이드

이 문서는 Actor 생성, Spot join/leave, 종료, 세션 바인딩 흐름을 설명한다.
SPOT 기본 설정과 디스패치 핸들러 등록은 [SPOT 가이드](07-3-spot.ko.md)를 본다.
정확한 함수 계약은 [SPOT spec](../spec/core/service/spot.ko.md)를 본다.

## 1. Actor로 세션 메시지 분배하기

Actor는 세션 메시지를 특정 처리 단위로 모으고, Spot 디스패치 콜백에서
읽을 대상을 구분할 때 쓴다. 하나의 세션은 여러 Actor를 바인딩할 수 있고,
하나의 Actor는 동시에 하나의 세션에만 바인딩된다.

Actor는 생성 직후 `Entry Spot`에 속한다. `Entry Spot`은 `SpotNode`가 항상 가지고
있는 기본 Spot이다. `Entry Spot`에 디스패치 핸들러를 등록하면 새 Actor의 초기
메시지를 받아 인증하거나 대상 Spot을 선택할 수 있다.

Entry Spot 파사드는 아래처럼 얻는다.

```c
void *entry = NULL;
zlink_spot_node_entry_spot(node, &entry);
zlink_spot_dispatch_event_handler(entry, my_dispatch_handler, userdata);
```

Entry Spot 파사드를 다 쓴 뒤에는 `zlink_spot_destroy(&entry)`로 닫는다.
Entry Spot 자체는 `SpotNode`가 소유하므로 파사드를 닫아도 Entry Spot은 사라지지 않는다.

최소 흐름은 다음과 같다.

1. `SpotNode`에서 Actor를 만든다.
2. STREAM 클라이언트 세션 라우팅 ID를 확인한다.
3. `zlink_stream_bind_actor()`로 세션과 Actor를 연결한다.
4. STREAM 패킷 핸들러나 앱 로직에서 `zlink_stream_send_bound_actor_part()`를
   호출해 Actor ID를 지정한다.
5. 디스패치 콜백에서 `ACTOR_READABLE`을 받으면 `subject` Actor ref를 복사하고
   `zlink_spot_node_actor_recv_part()`로 소진(drain)한다.

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

Actor에 읽을 데이터가 생기면 디스패치 콜백이 소진할 Actor ref를 알려준다.

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

Actor 주소를 다른 노드에서 알아야 하면, Actor 소유 Discovery에서
`ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC`를 켜고 Actor를 STREAM 세션에 바인딩한 뒤
`zlink_discovery_resolve_actor()`로 조회한다. Actor 생성이나 Spot 참가만으로는
활성 경로(active route)가 공개되지 않는다. **활성 경로는 STREAM 세션 바인딩 성공 시점에만
게시된다.**

로컬 노드에서 ID로 기존 Actor를 조회하려면:

```c
zlink_actor_ref_t ref;
zlink_config_result_t rc = zlink_spot_node_actor_lookup(node, "player-42", &ref);
if (rc == ZLINK_CONFIG_OK) {
    /* Actor 존재 — ref 사용 가능 */
} else if (rc == ZLINK_CONFIG_NOT_FOUND) {
    /* 해당 id의 Actor 없음 */
}
```

원격 Actor가 필요하면 호출 노드에서 `zlink_spot_node_create_remote_actor()`를
사용한다. 대상 노드에 이미 같은 Actor ID가 있으면 새로 만들지 않고 기존
결과를 반환한다. 대상 노드가 입장 허용(admission) 핸들러에서 요청을 거부하면
요청은 거부 결과로 끝난다. 원격 생성-또는-조회는 대상 Spot 참가 핸들러를 거치지 않는다.
새로 만들어진 원격 Actor도 대상 노드의 Entry Spot에 속한다.

원격 Actor 생성 허용 여부를 제어하려면 `SpotNode`에 입장 허용 핸들러를 등록한다.
핸들러는 이 노드로 향하는 모든 `zlink_spot_node_create_remote_actor()` 요청에 대해
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

`NULL`을 전달하면 이전에 등록된 핸들러를 제거한다 (기본 동작: 모든 원격 생성 요청 허용).

## 2. Spot join

Actor를 사용자 Spot으로 보내려면 `zlink_spot_node_actor_join_spot()`으로 참가 요청을
전송한다. 대상 Spot은 `ACTOR_JOIN_READABLE` 디스패치 이벤트를 받고,
`zlink_spot_actor_join_recv()`로 요청 메시지를 읽은 뒤 `zlink_spot_actor_join_reply()`로
수락 또는 거부를 전달한다.

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

참가 대기 중에는 새 참가, 탈퇴, 소멸이 busy 계열 오류로 실패한다. Entry Spot이 아닌
사용자 Spot으로 참가하려면 해당 Actor에 바인딩된 STREAM 세션이 있어야 한다.
`timeout_ms`는 작업 타임아웃이며, 제출 단계의 비차단 여부는 `ZLINK_DONTWAIT`
플래그로 제어한다.

## 3. Spot leave

`leave`는 Actor를 현재 Spot에서 Entry Spot으로 돌려보내는 동작이다. Actor가 이미
Entry Spot에 있으면 멱등(idempotent) 성공으로 처리된다. 탈퇴 후 Actor 메시지는 Entry Spot
디스패치 이벤트로 올라간다.

```c
/* game Spot에서 Entry Spot으로 복귀 */
zlink_spot_node_actor_leave_spot(
  node, &actor_ref,
  &current_spot_rid,  /* Actor의 현재 Spot rid */
  2000);
```

`current_spot_rid`는 오래된 탈퇴 요청을 막기 위한 낙관적 검사(optimistic check)다. 호출자가 알고 있는
현재 Spot과 실제 현재 Spot이 다르면 잘못된 상태 오류로 실패한다. 참가 대기
중에는 탈퇴가 `EBUSY`로 실패하며, 탈퇴가 대기 중인 참가를 취소하지는 않는다.

## 4. Actor 종료

Actor 소멸은 Actor가 Entry Spot에 있을 때만 허용된다. 사용자 Spot에 있으면 먼저
`leave`로 Entry Spot으로 돌려보내야 한다.

```c
/* 먼저 leave */
zlink_spot_node_actor_leave_spot(node, &ref, &current_spot_rid, 2000);
/* Entry Spot 복귀 확인 뒤 destroy */
zlink_spot_node_actor_destroy(node, &ref, 2000);
```

바인딩된 STREAM 세션이 있는 상태에서 소멸(destroy)하면, 세션 Actor 목록 항목과 바인딩된
세션 참조가 먼저 정리된다. 클라이언트 연결 자체는 닫히지 않는다. 연결까지
함께 닫으려면 소멸 전에 `zlink_spot_node_actor_close_bound_session()`을 호출한다.

수신 트리거 없이 STREAM 클라이언트에 메시지를 밀어 넣으려면
`zlink_spot_node_actor_send_bound_session_msg()`를 사용한다. Actor에 활성 바인딩 세션이
있어야 하며, 없으면 호출이 실패한다.

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
