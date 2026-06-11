[← 목차](./README.ko.md)

# 6. SPOT

## 1. SPOT이 하는 일

SPOT은 room·stage·zone처럼 **"장소" 하나의 상태와 참가자를 묶는 실행 단위**다.
게임 룸 하나가 spot 인스턴스 하나다. actor([7장](./07-actor-session.ko.md))가
spot에 입장(join)하고, spot 안에서 패킷을 처리하고, 토픽으로 알림을 받는다.

- spot 인스턴스의 핸들러는 **그 인스턴스 안에서 직렬 실행**된다 — 룸 상태에
  락 없이 접근할 수 있다.
- spot은 `entry_spot_t`(노드당 1개, 입장/매칭 담당)와 `spot_t`(룸 본체)로
  나뉜다.

## 2. 노드 선언

spot은 spot mesh(디스커버리 채널) 아래 노드로 선언한다. Bingo Play 서버의 실제
구성:

```cpp
options.add_spot_mesh ("bingo.room.discovery")
  .add_node ("bingo.room.node")
  .enable_router (topology.play_spot_router_endpoint, topology.play_rid)
  .enable_pub_sub (topology.play_spot_endpoint)
  .attach_channel_client ("bingo.api")
  .add_entry_spot<bingo_entry_spot_t> ()
  .add_spot<bingo_room_spot_t> ("bingo.room");
```

| 빌더 | 의미 |
|------|------|
| `add_node(name)` | spot 노드 추가 |
| `enable_router(endpoint, rid)` | 노드 간 라우팅 수신 |
| `enable_pub_sub(endpoint)` | spot 토픽 pub/sub endpoint |
| `use_discovery(channel)` | registry 기반 노드 발견 ([10장](./10-registry.ko.md)) |
| `accept_routes_from_channel(channel, endpoint)` | route mesh 채널에서 라우트 수신 ([5장 §5](./05-channel-messaging.ko.md)) |
| `attach_channel_client(name)` / `attach_publisher(name)` | spot 코드에서 쓸 채널 client/publisher 연결 |
| `add_entry_spot<T>()` | 입장 spot 등록 (노드당 1개) |
| `add_spot<T>(name)` | spot 타입 등록 |
| `add_actor_factory<F>(type)` | actor factory 등록 ([7장](./07-actor-session.ko.md)) |
| `enable_actor_gateway()` | actor gateway 활성화 ([7장](./07-actor-session.ko.md)) |

## 3. Spot 클래스 작성

`spot_t`(또는 `entry_spot_t`)를 상속하고 `configure`에서 패킷 핸들러를 등록한다.

```cpp
class bingo_room_spot_t : public zlink::framework::spot_t, public bingo_room_game_t
{
  public:
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&bingo_room_spot_t::submit_card> ();
    }

    // actor 패킷 핸들러: (actor, 요청 컨텍스트, typed 요청) → typed 응답
    submit_bingo_card_res_t
    submit_card (const player_actor_t &actor,
                 const zlink::framework::spot_actor_request_context_t &context,
                 const submit_bingo_card_req_t &request)
    {
        return bingo_room_game_t::submit_card (actor.actor.actor_id, request.card);
    }

    // 입장 수락/거부
    zlink::framework::spot_actor_join_response_t
    on_actor_join (const player_actor_t &actor, const zlink::message_t &request_message)
    {
        join (actor.actor.actor_id, actor.display_name);
        return zlink::framework::spot_actor_join_response_t::accept (
          to_stream_payload (bingo_room_join_res_t{snapshot ()}));
    }

    void on_post_actor_joined (const player_actor_t &actor) { /* 입장 완료 후 */ }
    void on_actor_left (const player_actor_t &actor) { leave (actor.actor.actor_id); }
};
```

수명주기 훅 요약:

| 훅 | 시점 |
|----|------|
| `configure(context)` | spot 생성 시 — 핸들러/타이머 등록 |
| `on_actor_join(actor, msg)` | 입장 요청 — `accept(reply)`/`reject(reply)` 반환 |
| `on_post_actor_joined(actor)` | 입장 확정 후 |
| `on_actor_left(actor)` | 퇴장 |

핸들러는 동기 반환 또는 `task_t<...>` 코루틴 둘 다 가능하다([3장 §4·§5](./03-concepts.ko.md)).

## 4. entry spot: 매칭과 룸 배정

entry spot은 룸에 들어가기 전 단계 — 매칭, 룸 생성/배정 — 를 담당한다.

```cpp
class bingo_entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    void configure (zlink::framework::spot_context_t &context)
    {
        context.handlers ().add_actor_packet<&bingo_entry_spot_t::match_bingo> ();
    }

    match_bingo_res_t match_bingo (const player_actor_t &actor,
                                   zlink::framework::spot_actor_request_context_t &,
                                   const match_bingo_req_t &request)
    {
        const auto room_id = rooms.allocate (request.mode);
        return {room_id, rooms.get (room_id).snapshot ()};
    }

    bingo_room_allocator_t rooms;
};
```

## 5. Timer

spot 안의 주기 작업은 `spot_context_t::add_timer<THandler>`로 등록한다.
`THandler`는 tick을 받는 별도 핸들러 클래스이고, tick은 spot 핸들러와 같은
직렬 실행 보장을 받는다.

```cpp
class draw_tick_handler_t final
{
  public:
    void handle (bingo_room_spot_t &spot, const zlink::framework::timer_tick_t &tick)
    {
        spot.draw_numbers (tick.skipped_ticks + 1);
    }
};

void configure (zlink::framework::spot_context_t &context)
{
    _draw_timer = context.add_timer<draw_tick_handler_t> (
      "draw-number", std::chrono::seconds (5),
      {.overrun_policy = zlink::framework::timer_overrun_policy_t::skip_late_ticks});
}
```

| `timer_overrun_policy_t` | 늦은 tick 처리 |
|--------------------------|----------------|
| `skip_late_ticks` (기본) | 늦은 tick은 건너뜀 (`timer_tick_t::skipped_ticks`로 보고) |
| `catch_up_bounded` | `max_catch_up_ticks`까지 따라잡기 |
| `delay_next_tick` | 다음 tick을 밀어서 간격 유지 |

`timer_t::cancel()`로 중지한다. tick 핸들러의 미처리 예외 정책은
`stop_on_unhandled_exception`으로 정한다. timer 이벤트는
[11장 모니터링](./11-monitoring.ko.md)의 `add_spot_timer_events`로 관측한다.

## 6. spot에서 바깥으로 보내기

- 노드에 `attach_channel_client("bingo.api")`를 걸어 두면 spot 코드에서 그
  채널로 request/send를 보낼 수 있다.
- `attach_publisher(channel)`로 fanout publish 경로를 연결한다.
- 토픽 구독자(클라이언트)에게 가는 알림은 `enable_pub_sub` endpoint를 통해
  spot 토픽으로 발행된다.

[다음: Actor · Session →](./07-actor-session.ko.md)
