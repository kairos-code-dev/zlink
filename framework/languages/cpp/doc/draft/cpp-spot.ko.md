<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Registry](./cpp-registry.ko.md) | [다음: Draft -- ZLink Framework C++ STREAM](./cpp-stream.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 `SPOT`을 어떤 표면으로 통합할지
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

SPOT node는 `use_zlink(...)` 안에서 구성한다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("stage-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry:5551");
      })
      .channel("game.stage", [](auto &channel) {
          channel.enable_publisher();
          channel.enable_subscriber([](auto &subscriber) {
              subscriber.use_discovery();
          });
      })
      .spot_node("stage-spot-node", [](auto &spot_node) {
          spot_node.bind("tcp://0.0.0.0:9000");
          spot_node.enable_actor_gateway();
          spot_node.use_discovery("game.stage");
          spot_node.attach_channel_client("profile");
          spot_node.attach_publisher("game.stage");
          spot_node.add_entry_spot<player_entry_spot_t>();
          spot_node.add_actor_factory<player_actor_factory_t>("player");
          spot_node.add_spot<stage_spot_t>("stage");
      });
});
```

`spot_node.use_discovery(channel_name)`의 `channel_name`은 active SPOT channel view를
뜻한다. 같은 SPOT node가 여러 channel capability를 attach할 수 있으므로 discovery
대상 이름을 생략하지 않는다.

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

구현은 CAPI timer를 래핑한다. CAPI timer는 interval wakeup과 누적 `fire_count`를
제공하고, framework는 그 값을 이용해 `.NET` framework timer와 같은 tick metadata와
overrun policy를 만든다. 사용자는 native timer handle, poller slot, timer recv 순서를
직접 다루지 않는다.

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

- user Spot timer는 같은 user Spot의 packet, actor packet, subscription과 같은 core
  SPOT dispatch boundary에서 순서 정책을 따른다.
- Entry Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다.
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

Entry Spot과 user Spot의 actor handler 등록 표면은 같아도 실행 위치는 다르다.

| 입력 경로 | 실행 위치 |
|-----------|-----------|
| Entry Spot actor packet | core actor ordering |
| user Spot actor packet | core SPOT dispatch boundary |
| user Spot packet / timer / subscription | core SPOT dispatch boundary |
| Entry Spot timer | Entry Spot 전체 전역 직렬화 경계에 묶지 않음 |

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
- 같은 SPOT node 안의 ordering과 handler concurrency 정책은 core dispatch boundary를
  기준으로 framework가 typed handler 표면에 투영한다.
- CPU-bound 또는 blocking 가능성이 있는 handler는 framework core의 offload 실행
  정책을 명시해 별도 thread에서 처리한다.
- SPOT timer는 native timer handle이 아니라 CAPI timer 등록을 감싼 framework timer
  handle로 노출한다. `.NET` timer와 같은 기능성은 CAPI timer 위에서 framework가
  metadata, overrun policy, dispatch 경계를 추가해 맞춘다.
- session actor relay는 ActorGateway 경로를 사용하고 application route mesh channel로
  우회하지 않는다.
