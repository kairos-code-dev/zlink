[English](07-4-actor.md) | [한국어](07-4-actor.ko.md)

<!-- zlink-nav:start -->
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
<!-- zlink-nav:end -->

# SPOT Actor 사용 가이드

이 문서는 Actor 생성, Spot join/leave, 종료, 세션 바인딩 흐름을 설명한다.
SPOT 기본 설정과 dispatch 핸들러 등록은 [SPOT 가이드](07-3-spot.ko.md)를 본다.
정확한 함수 계약은 [SPOT spec](../api/spot.ko.md)를 본다.

> Actor가 **무슨 역할이고 언제** 쓰는지(세션↔처리 단위 binding, 재접속 이전성,
> plain Spot과의 차이)는
> [서비스 개요 §멘탈 모델](07-0-services.ko.md#12-멘탈-모델--어느-층을-언제-쓰나)을
> 먼저 본다.

## Actor 위치 조회와 전송

Actor id만으로 원격 위치를 조회하는 공개 core API는 현재 제공하지 않는다.
Actor 이동 상태는 SPOT/Actor 생명주기 안에서 관리된다.

이 흐름에서 target Spot에 도착한 메시지를 어떤 Actor에게 넘길지는 application이
정의하는 packet/handler 계약이다. core는 `router -> actor` direct send/request API를
추가하지 않는다.

## 1. Actor로 세션 메시지 묶기

Actor는 세션 메시지를 특정 처리 단위로 모으고, Spot dispatch 콜백에서
읽을 대상을 구분할 때 쓴다. 하나의 세션은 여러 Actor를 바인딩할 수 있고,
하나의 Actor는 동시에 하나의 세션에만 바인딩된다.

Actor는 생성 직후 `Entry Spot`에 속한다. `Entry Spot`은 `SpotNode`가 항상 가지고
있는 기본 Spot이다. `Entry Spot`에 dispatch 핸들러를 등록하면 새 Actor의 초기
메시지를 받아 인증하거나 대상 Spot을 선택할 수 있다.

Entry Spot facade는 아래처럼 얻는다.

```c
void *entry = NULL;
zlink_spot_node_entry_spot(node, &entry);
zlink_spot_dispatch_event_handler(entry, my_dispatch_handler, userdata);
```

Entry Spot facade를 다 쓴 뒤에는 `zlink_spot_destroy(&entry)`로 닫는다.
Entry Spot 자체는 `SpotNode`가 소유하므로 facade를 닫아도 Entry Spot은 사라지지 않는다.

최소 흐름은 다음과 같다.

1. `SpotNode`에서 Actor를 만든다.
2. STREAM 클라이언트 세션 라우팅 ID를 확인한다.
   연결한다.
4. `zlink_stream_bind_actor()`로 세션과 Actor를 연결한다.
5. STREAM 패킷 핸들러나 앱 로직에서 `zlink_stream_send_bound_actor_part()`를
   호출해 Actor ID를 지정한다.
6. dispatch 콜백에서 `ACTOR_READABLE`을 받으면 `subject` Actor ref를 복사하고
   `zlink_spot_node_actor_recv_part()`로 소진(drain)한다.

```c
zlink_actor_ref_t ref;
zlink_spot_node_actor_new(node, "player-42", &ref);


/* async submit; bind completion fires via reply handler */
zlink_stream_bind_actor(stream, &session_rid, &ref,
                        my_bind_handler, userdata, 2000);

zlink_msg_t part;
zlink_msg_init_size(&part, 5);
memcpy(zlink_msg_data(&part), "hello", 5);
zlink_stream_send_bound_actor_part(
  stream,
  &session_rid,
  "player-42",
  &part,
  0,
  ZLINK_PART_FINAL);
```

Actor에 읽을 데이터가 생기면 dispatch 콜백이 소진할 Actor ref를 알려준다.

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

Actor 위치는 SPOT/Actor 생명주기에 속한다. Actor를 만들면 Entry Spot에 놓이고,
user Spot join이 성공하면 join한 user Spot으로 이동하며, 명시적 leave가
성공하면 다시 Entry Spot으로 돌아간다. STREAM 세션 바인딩이나 해제는 Actor
위치를 바꾸지 않는다.

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

원격 노드에 존재하는 Actor의 checked ref가 필요하면
`zlink_remote_actor_get_ref()`로 비동기 lookup을 수행한다. 이 함수는 submit에서 blocking
하지 않고 lookup 완료를 callback으로 전달한다. 대상 노드에 Actor가 있으면
`result->actor`에 checked ref가 들어 있고, 없으면 not-found 계열 completion으로
끝난다.

```c
static void on_lookup(
    const zlink_actor_lookup_result_t *result, void *userdata)
{
    if (result->result == ZLINK_REQUEST_OK) {
        zlink_actor_ref_t ref = result->actor;  /* callback 안에서 복사 */
        /* ref를 join, bind, destroy 등에 사용 */
    } else {
        /* not-found, not-connected, timed-out 등 */
    }
}

zlink_remote_actor_get_ref(
    node,                /* lookup 요청을 제출하는 SpotNode */
    &target_node_rid,
    "player-42",
    on_lookup,
    NULL,                /* userdata */
    3000);               /* timeout_ms */
```

원격 노드에서 시작해야 하는 Actor는 application이 해당 SpotNode에서
`zlink_spot_node_actor_new()`로 직접 생성한다. 같은 process 안에 SpotNode handle이
있다면 그 handle을 사용하고, 다른 process라면 원격 SpotNode가 제공하는 application
계층 RPC로 생성 요청을 전달한다. 생성 뒤 필요하면 `zlink_spot_node_actor_join_spot()`
으로 원하는 user Spot에 이동하고, join completion이 반환한 최종 Actor ref를 후속
작업에 사용한다.

## 2. Spot join

Actor를 사용자 Spot으로 보내려면 `zlink_spot_node_actor_join_spot()`으로 참가 요청을
전송한다. 대상 Spot은 `ACTOR_JOIN_READABLE` dispatch 이벤트를 받고,
`zlink_spot_actor_join_recv()`로 요청 payload를 읽은 뒤 `zlink_spot_actor_join_reply()`로
수락 또는 거부를 전달한다. join completion은 전용 `zlink_actor_join_spot_handler_fn`으로
전달되며, 최종 Actor ref와 joined Spot rid를 포함한다.

```c
static void on_join(
    const zlink_actor_join_result_t *result,
    zlink_msg_t *parts, size_t part_count, void *userdata)
{
    if (result->result == ZLINK_REQUEST_OK) {
        /* 성공: result->actor가 최종 Actor ref (remote join이면 target node ref) */
        zlink_actor_ref_t final_ref = result->actor;
        /* 후속 Actor API나 위치 이동에 final_ref를 사용 */
    }
    zlink_multipart_close(parts, part_count);
}

zlink_spot_node_actor_join_spot(
  node, &actor_ref,
  &dest_node_rid, &dest_spot_rid,
  &payload_msg, 1,    /* join state payload parts */
  on_join,            /* zlink_actor_join_spot_handler_fn completion */
  userdata,
  0, 3000);           /* flags, timeout_ms */

/* target Spot dispatch callback에서 */
case ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE: {
    zlink_actor_join_info_t info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    while (zlink_spot_actor_join_recv(spot_, &info, &parts, &part_count,
                                      ZLINK_DONTWAIT) == ZLINK_RECV_OK) {
        /* info.flags & ZLINK_ACTOR_JOIN_INFO_REMOTE 이면 remote join */
        int join_result = 0; /* 0 = 수락; 0이 아니면 거부 코드 */
        zlink_spot_actor_join_reply(spot_, &info, join_result, NULL, 0);
        zlink_multipart_close(parts, part_count);
    }
    break;
}
```

참가 대기 중에는 새 참가, 탈퇴, 소멸이 busy 계열 오류로 실패한다. **session 바인딩이
없어도 user Spot으로 join할 수 있다.** Actor 위치 이동과 session attach는 서로 다른
상태 전이이기 때문이다. `join_spot`의 `dest_spot_rid`는 target node의 user Spot이어야
하며 Entry Spot은 target이 아니다. 같은 Spot에 대한 idempotent join은 admission 없이
성공 completion으로 끝난다. `timeout_ms`는 작업 타임아웃이며, 제출 단계의 비차단
여부는 `ZLINK_DONTWAIT` 플래그로 제어한다.

> target user Spot에 dispatch handler가 없으면 join request는 자동 accept되지 않는다.
> `timeout_ms > 0`이면 timeout까지 pending이고, `timeout_ms == 0`이면 handler가
> 등록되어 처리하거나 Spot/SpotNode가 종료될 때까지 pending 상태로 남는다.

### Spot lifecycle event

Actor의 위치 변경(생성, join, leave, destroy)을 관측하려면 Actor 전이가 발생하기 전에 `zlink_spot_dispatch_event_handler()`를 등록한다. `ACTOR_LIFECYCLE_READABLE`이 오면 `zlink_spot_recv_actor_lifecycle()`로 event를 drain한다.

```c
zlink_spot_actor_lifecycle_event_t event;
while (zlink_spot_recv_actor_lifecycle(spot, &event, ZLINK_DONTWAIT) == ZLINK_RECV_OK) {
    if (event.kind == ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED) {
        /* event.info.current_actor가 이 Spot에 들어온 Actor */
    }
}
```

lifecycle event는 관측용이다. application state machine이 join 완료나 session join 완료 순서를 결정할 때는 join completion handler와 반환된 최종 Actor ref를 기준으로 삼는다.

## 3. Spot leave

`leave`는 Actor를 현재 Spot에서 같은 node의 Entry Spot으로 돌려보내는 async submit
API다. Actor가 이미 Entry Spot에 있으면 멱등(idempotent) 성공이고, lifecycle event은
발생하지 않는다. 탈퇴 후 Actor 메시지는 Entry Spot dispatch 이벤트로 올라간다.

```c
static void on_leave(
    zlink_request_result_t result,
    zlink_msg_t *parts, size_t part_count, void *userdata)
{
    /* completion payload는 없음. result로 성공/실패 판단 */
}

zlink_spot_node_actor_leave_spot(
  node, &actor_ref,
  &current_spot_rid,  /* Actor의 현재 Spot rid */
  on_leave,
  userdata,
  2000);
```

`current_spot_rid`는 오래된 탈퇴 요청을 막기 위한 낙관적 검사(optimistic check)다.
호출자가 알고 있는 현재 Spot과 실제 현재 Spot이 다르면 invalid-state 계열 실패다.
참가 대기 중에는 탈퇴가 busy로 실패하며, 탈퇴가 대기 중인 참가를 취소하지는 않는다.
leave 성공으로 user Spot에서 Entry Spot으로 위치가 바뀌면 active route가 Entry Spot
위치로 갱신된다.

## 4. Actor 종료

Actor 소멸은 Actor가 Entry Spot에 있을 때만 성공한다. 사용자 Spot에 있으면 먼저
`leave`로 Entry Spot으로 돌려보내야 한다. destroy 역시 async submit API다.

```c
static void on_destroy(
    zlink_request_result_t result,
    zlink_msg_t *parts, size_t part_count, void *userdata) { /* ... */ }

/* 먼저 leave 후 leave completion에서 destroy submit */
zlink_spot_node_actor_destroy(node, &ref, on_destroy, userdata, 2000);
```

destroy 성공 시 Entry Spot의 Actor slot이 제거되고, active route가 같은 ref를 가리키면
route도 함께 제거된다. session attach 상태는 Actor 위치와 독립이므로 destroy 전에
별도로 session attach를 해제해야 한다면 `zlink_stream_unbind_actor()`를 호출한다.
연결까지 닫아야 하면 `zlink_spot_node_actor_close_bound_session()`을 사용한다.
STREAM 세션이 닫히거나 끊기면 그 Actor 바인딩은 자동으로 정리되지만, 이때 Actor가
Entry Spot으로 이동하거나 join한 Spot이 바뀌지는 **않는다**. 세션의 bound Actor를
열거하려면 `zlink_stream_bound_actors()`를 호출한다.

수신 트리거 없이 STREAM 클라이언트에 메시지를 밀어 넣으려면
`zlink_spot_node_actor_send_bound_session_msg()`를 사용한다. Actor에 활성 바인딩 세션이
있어야 하며, 없으면 호출이 실패한다.

framework adapter 를 사용할 때 session handler 는 actor route resolver 나 route mesh channel을
직접 고르지 않는다. STREAM은 session owner `SpotNode`의 ActorGateway에 attach되고, client
message relay 는 logical Actor binding 을 통해 전달된다.

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

---
<!-- zlink-nav:bottom:start -->
[← SPOT](07-3-spot.ko.md) | [Routing ID →](08-routing-id.ko.md)
<!-- zlink-nav:bottom:end -->
