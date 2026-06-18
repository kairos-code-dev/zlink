<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md) | [다음: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](../internals/cpp-framework-policy.ko.md) | [Framework 인터페이스](cpp-framework-interfaces.ko.md) | [SPOT 샘플](../internals/spot-samples.ko.md) | [Stage wrapper](stage-wrapper-on-spot.ko.md)

# Spec -- ZLink Framework C++ SPOT

> 이 문서는 **구현 완료된 설계 계약**이다.
> `C++` host/runtime에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 인터페이스 경계

SPOT public contract는 `contracts/spots/*`가 소유한다. public 표면에는
`spot_node_builder_t`, `spot_context_t`, `SpotRid`, `NodeRid`, packet registry view,
Entry Spot과 actor factory 등록 계약만 둔다. Spot activation table, native dispatch
router, subscription pump, routed relay packet dispatcher, discovery reconciler는
`src/runtime/spots/*`와 관련 runtime 영역에 둔다.

직접 `rid`를 사용하는 public API는 spot-to-spot 메시징과 Entry Spot join 같은 actor
lifecycle 경로에 제한한다. 일반 handler와 client는 channel name, topic, typed payload를
먼저 사용한다.

## 1. 방향

`SPOT`은 lightweight distributed endpoint다. actor와 비슷하지만 네트워크, 라우팅,
분산 runtime을 먼저 고려한다.

`C++` framework는 binding의 `zlink::service::spot_node_t`와
`zlink::service::spot_t`를 직접 노출하지 않고, 아래 표면으로 감싼다.

- `spot_node_builder_t`
- `spot_context_t`
- `timer_t`, `timer_options_t`, `timer_tick_t`
- actor factory와 `actor_context_t`
- `module_t` 또는 DI 기반 spot owner 등록

일반 application handler는 channel/topic 중심으로 시작한다. 직접 `routing_id_t`를
받는 API는 spot-to-spot send/request와 Entry Spot join 경로에 제한한다.

## 2. Spot node 구성

SPOT node는 `add_zlink_framework(...)` 안의 options builder에서 구성한다. 사용자는 core
builder의 channel, subscriber, discovery 람다를 직접 조립하지 않는다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.add_client_server_channel("profile")
      .enable_client();
    options.add_fanout_channel("game.stage")
      .enable_publisher("tcp://0.0.0.0:7001");
    options.add_spot_mesh("game.stage")
      .add_node("stage-spot-node")
      .enable_pub_sub("tcp://0.0.0.0:9000")
      .enable_actor_gateway()
      .attach_channel_client("profile")
      .attach_publisher("game.stage")
      .add_entry_spot<player_entry_spot_t>()
      .add_actor_factory<player_actor_factory_t>("player")
      .add_spot<stage_spot_t>("stage");
});
```

`spot_node.use_discovery(channel_name)`의 `channel_name`은 active SPOT channel view를
뜻한다. 같은 SPOT node가 여러 channel 역할을 attach할 수 있으므로 discovery
대상 이름을 생략하지 않는다.

registry discovery를 쓰지 않는 topology에서는 attach별 manual endpoint를 명시한다.

```cpp
options.spot_node("stage-spot-node")
  .enable_router("tcp://0.0.0.0:9000", zlink::routing_id_t::from("stage-router"))
  .connect_router("tcp://127.0.0.1:9001")
  .enable_pub_sub("tcp://0.0.0.0:9002")
  .connect_pub_sub("tcp://127.0.0.1:9003")
  .attach_channel_client("profile", "tcp://127.0.0.1:7001");
```

`connect_router(...)`와 `connect_pub_sub(...)`의 manual endpoint는
SPOT 역할 자체의 peer다. `attach_channel_client(...)`,
`attach_publisher(...)`, `accept_routes_from_channel(...)`에 주는 manual endpoint는 각각
attached channel client, publisher client, accepted route ingress의 peer이므로 같은 값으로
섞어 표현하지 않는다.

## 3. Spot context

spot 내부 코드는 framework가 주입하는 `spot_context_t`를 통해 publish, direct routing,
channel request를 수행한다.

```cpp
class stage_spot_t final {
public:
    explicit stage_spot_t(zlink::framework::spot_context_t &context)
      : context_(context)
    {
    }

    void publish_state(const stage_state_updated_t &event)
    {
        context_.publish("stage.state.updated", event);
    }

    zlink::framework::request_call_t<profile_reply_t> load_profile(
      profile_query_t query)
    {
        return context_.request_to<profile_reply_t>(
          target_node_rid_,
          target_spot_rid_,
          query);
    }

private:
    zlink::framework::spot_context_t &context_;
    zlink::routing_id_t target_node_rid_;
    zlink::routing_id_t target_spot_rid_;
};
```

`spot_context_t::publish(...)`는 현재 SPOT channel 안의 topic publish를 뜻하므로
별도 channel name을 받지 않는다. 일반 외부 publisher는 `publisher_t`를 사용한다.

## 4. Timer

SPOT timer는 server-side timer다. client rendering frame이나 input polling 용도가
아니다. cleanup, heartbeat, timeout sweep, room tick, match tick을 같은
`add_timer(...)` 표면으로 처리한다.

구현은 CAPI timer를 사용한다. CAPI timer는 interval wakeup, dispatch event, 누적
`fire_count`, stop/destroy lifecycle을 제공하고, framework는 그 값을 이용해 `.NET`
framework timer와 같은 tick metadata와 overrun policy를 만든다. 사용자는 native timer
handle, poller slot, timer recv 순서를 직접 다루지 않는다.

C++ framework timer는 CAPI SPOT dispatch event 뒤에 timer recv를 수행하는 경로를
실행 직렬화 경계로 사용한다. 따라서 다른 binding처럼 자체 timer를 따로 만들고
callback 실행 직렬화를 위한 별도 queue를 추가하지 않는다. C++에서는 CAPI timer를
사용하는 것이 binding 호출 오버헤드를 늘리지 않고, core가 이미 제공하는 SPOT dispatch
ordering을 그대로 framework handler 표면에 투영한다.

```cpp
timer_ = context_.add_timer<stage_tick_handler_t>(
  "stage-tick",
  std::chrono::milliseconds(16),
  {.overrun_policy = zlink::framework::timer_overrun_policy_t::skip_late_ticks});
```

timer handler는 tick metadata를 받아야 한다.

```cpp
class stage_tick_handler_t final {
public:
    void handle(stage_spot_t &spot,
      const zlink::framework::timer_tick_t &tick)
    {
        spot.advance(tick.period, tick.skipped_ticks + 1);
    }
};
```

실행 정책은 아래와 같다.

- user Spot timer는 같은 user Spot의 packet, actor packet, subscription과 같은 CAPI
  SPOT dispatch event 후 recv 경계에서 순서 정책을 따른다.
- Entry Spot timer는 Entry Spot actor packet, lifecycle callback, request continuation과
  같은 Entry Spot 실행 줄에서 처리한다.
- 같은 timer instance는 callback을 겹쳐 실행하지 않는다.
- `skip_late_ticks`, `catch_up_bounded`, `delay_next_tick` 정책을 제공한다.
- `fire_count` 누적값으로 missed tick을 계산하고 `skipped_ticks`와 `scheduled_index`에
  반영한다.
- timer handler 예외는 monitoring event로 즉시 기록하고, option에 따라 timer를
  계속 실행하거나 중단한다.

## 5. Actor / Entry Spot

Actor는 SpotNode에 소속되고, 생성 직후 Entry Spot에 위치한다. STREAM session에 bind될
수 있지만, session binding은 actor 위치를 결정하지 않는다. user Spot join은 별도
lifecycle 작업이다.

Entry Spot과 user Spot의 actor packet 등록 표면은 같아도 실행 위치는 다르다.

| 입력 경로 | 실행 위치 |
|-----------|-----------|
| Entry Spot actor packet | core actor ordering |
| user Spot actor packet | CAPI SPOT dispatch event 후 recv 경계 |
| user Spot packet / timer / subscription | CAPI SPOT dispatch event 후 recv 경계 |
| Entry Spot timer | Entry Spot 실행 queue |

Spot join admission은 registry handler가 아니라 Spot member callback이다. callback은
`on_actor_join(actor, message_t)` 형태이며, `spot_actor_join_response_t`로 accepted 여부와
optional reply `message_t`를 돌려준다. accepted가 `true`일 때만 actor 위치를 target Spot으로
commit하고 `on_actor_joined(actor)`를 호출한다. accepted가 `false`이면 actor 위치를
바꾸지 않고 post-joined callback도 호출하지 않는다. Entry Spot으로 돌아오는 명시적
join도 같은 admission callback을 사용하며, commit 이후 `on_actor_joined(actor)`를 호출한다.

actor 수명을 끝내는 API는 Entry Spot context에만 둔다. user Spot context에는 destroy
API가 없다. actor가 user Spot에 있으면 먼저 user Spot에서 leave를 완료해 Entry Spot으로
되돌린 뒤 아래 API를 호출한다.

```cpp
zlink::framework::task_t<void> destroy =
  entry_context.destroyActor(actor_ref, actor);
```

`destroyActor(actor_ref, actor)`는 actor가 현재 Entry Spot에 있을 때만 성공한다. 성공한
호출은 lifecycle callback을 호출하지 않고 actor membership을 지운다. 같은 actor instance에
대한 중복 호출은 성공으로 끝난다. 이전 generation의 stale actor ref는 새 actor를 지우지
않는다.

Spot create callback은 단일 `message_t` request를 받는다. payload 없이 create하면 빈
`message_t`를 전달한다. C++에서는 기본 생성 가능한 Spot을 자동 생성하고, 생성자 인자가
필요한 Spot은 `add_spot<TSpot>(name, factory)` 또는 `add_entry_spot<TEntrySpot>(factory)`로
factory를 등록한다. 이 factory는 `.NET`의 activation/DI 역할에 해당하며, 생성된 Spot instance는
`configure(context)`, `on_create(message_t)`, `on_initialize()` 순서로 lifecycle을 탄다.
create result는 `spot_rid`, `existing`/`created`/`rejected` state, optional reply `message_t`를
담는다. `get_or_create`는 이미 있으면 `existing`, 새로 만들면 `created`, create callback이
거부하면 `rejected`를 돌려준다. 같은 SpotRid로 동시에 `get_or_create`가 들어오면 첫 request만
create callback으로 전달한다.

`spot_context_t::close()`는 현재 Spot을 닫는 표면이다. actor가 남아 있는 user Spot은 닫히면
안 되므로 실패를 반환하고 find/list에서도 계속 조회 가능해야 한다. actor가 없는 Spot은 close
성공 후 find/list에서 사라지고 `on_closing` lifecycle이 호출된다. timer handler나 packet
handler 안에서 close를 요청하면 현재 callback이 끝난 뒤 닫는다. C++ SPOT dispatch는
CAPI dispatch event 후 recv 경계에서 이미 실행 직렬화되므로 close 처리를 위해 별도
실행 직렬화 queue를 추가하지 않는다.

current Spot 밖에서 target Spot으로 직접 send/request 하는 public client는 기본 표면에
두지 않는다. actor 생성 또는 Entry Spot join으로 actor handle을 얻고, session이
필요하면 session actor handle로 bind한다.

## 6. Backpressure

SPOT send path의 send-ready callback과 pending queue는 runtime 내부 구현이다. public
표면은 call object, timeout, result error kind로 backpressure를 보여 준다. application
handler가 pending queue를 직접 resume하거나 poller readiness를 직접 다루는 API는 두지
않는다.

기본 정책은 무한 queue가 아니다. queue 상한, submit timeout, overflow 정책은 framework
runtime 설정으로 닫고, 한도 초과는 `request_rejected` 같은 실패 result로 돌려준다.

## 7. 중요한 규칙

- SPOT discovery 설정은 channel view 이름을 명시한다.
- spot-to-spot send/request 외의 일반 흐름은 channel name과 topic을 먼저 사용한다.
- SPOT node lifecycle은 app host가 관리한다.
- handler callback은 framework가 packet 수신과 typed payload 변환을 마친 뒤 호출한다.
- 같은 SPOT node 안의 ordering과 handler concurrency 정책은 CAPI SPOT dispatch event 후
  recv 경계를 기준으로 framework가 typed handler 표면에 투영한다.
- CPU-bound 또는 blocking 가능성이 있는 handler는 framework core의 offload 실행
  정책을 명시해 별도 thread에서 처리한다.
- SPOT timer는 native timer handle이 아니라 CAPI timer 등록을 감싼 framework timer
  handle로 노출한다. `.NET` timer와 같은 기능성은 CAPI timer 위에서 framework가
  metadata, overrun policy, dispatch 경계를 추가해 맞춘다.
- C++ framework는 CAPI timer와 CAPI SPOT dispatch event 후 recv 경계를 사용하므로
  timer callback 실행 직렬화를 위한 별도 queue나 자체 timer scheduler를 만들지 않는다.
- session actor relay는 ActorGateway 경로를 사용하고 application route mesh channel로
  우회하지 않는다.

## 8. 회귀 테스트

SPOT 회귀 테스트는 `.NET` framework의 Spot, actor, timer 기대값을 C++ host 모델로 고정한다.
핵심은 Spot lifecycle, dispatch ordering, timer metadata, remote address resolution이 서로
다른 API처럼 갈라지지 않는지 확인하는 것이다.

필수 항목:

- Spot node는 app host start/stop lifecycle에 묶여 생성되고 정리된다.
- user Spot create/destroy, join/leave, actor join/left handler가 순서대로 호출된다.
- 같은 user Spot 안의 packet, actor packet, subscription, timer callback은 CAPI SPOT
  dispatch event 후 recv 경계 기준 ordering을 따른다.
- Entry Spot timer는 Entry Spot actor packet, lifecycle callback, request continuation과
  같은 Entry Spot 실행 줄에서 처리하고, 같은 timer instance의 재진입도 막는다.
- `publish(...)`, `request_to(...)`, actor packet handler가 typed DTO와 serializer registry를
  사용한다.
- Spot remote address lookup은 Registry/discovery 결과를 사용하고 stale address를 계속
  사용하지 않는다.
- duplicate resolver, ambiguous route channel, missing serializer는 startup validation에서
  실패한다.
- timer는 `fire_count` 기반 skipped tick, `scheduled_index`, overrun policy, cancel,
  handler exception monitoring을 검증한다.
- handler exception은 monitoring event와 log에 남고 runtime 전체를 죽이지 않는다.
- CPU-bound handler offload를 설정한 경우 shutdown drain에 포함된다.

CTest label은 `framework-zlink-spot`을 사용한다. timer 전용 항목은 `timer` label에도
포함할 수 있다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ Registry](cpp-registry.ko.md) | [다음: Spec -- ZLink Framework C++ STREAM](cpp-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
