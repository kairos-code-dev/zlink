<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Interface Alignment](./handler-interfaces.ko.md) | [다음: Draft -- C++ Stage Wrapper On SPOT](./stage-wrapper-on-spot.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [SPOT](./cpp-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `SPOT` 초안을 샘플로 보기 위한 문서다.

## 1. 등록과 `spot_name`

```cpp
auto app = zlink::framework::app_t::create();

app.use_zlink([](auto &zlink) {
    zlink.node("stage-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry1:5551");
      })
      .channel("game.stage", [](auto &channel) {
          channel.enable_publisher();
          channel.enable_subscriber([](auto &subscriber) {
              subscriber.use_discovery();
          });
      })
      .channel("profile", [](auto &channel) {
          channel.enable_client([](auto &client) {
              client.use_discovery();
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
          spot_node.add_spot<room_spot_t>("room");
      });
});
```

## 2. spot 객체와 publish

```cpp
class stage_spot_t final {
public:
    explicit stage_spot_t(zlink::framework::spot_context_t &context)
      : context_(context)
    {
    }

    void initialize()
    {
        context_.on_send_ready([](auto &ready) {
            ready.resume_pending();
        });

        timer_ = context_.add_timer<stage_tick_handler_t>(
          "stage-tick",
          std::chrono::milliseconds(16),
          {.overrun_policy =
             zlink::framework::timer_overrun_policy_t::skip_late_ticks});
    }

    void update_state(const stage_state_updated_t &event)
    {
        context_.publish("stage.state.updated", event);
    }

private:
    zlink::framework::spot_context_t &context_;
    zlink::framework::timer_t timer_;
};

class stage_tick_handler_t final {
public:
    void handle(stage_spot_t &spot,
      const zlink::framework::timer_tick_t &tick)
    {
        spot.advance(tick.period, tick.skipped_ticks + 1);
    }
};
```

## 3. spot-to-spot request

```cpp
class room_spot_t final {
public:
    explicit room_spot_t(zlink::framework::spot_context_t &context)
      : context_(context)
    {
    }

    zlink::framework::request_call_t<profile_reply_t> load_profile(
      zlink::routing_id_t node_rid,
      zlink::routing_id_t profile_spot_rid,
      profile_query_t query)
    {
        return context_.request_to<profile_reply_t>(
          node_rid,
          profile_spot_rid,
          query);
    }

private:
    zlink::framework::spot_context_t &context_;
};
```

직접 `routing_id_t`를 받는 API는 spot-to-spot과 Entry Spot join 경로에 제한한다.

## 4. 외부 노드에서 event publish

```cpp
publisher.publish(
  "game.stage",
  "stage.state.updated",
  build_stage_state_updated());
```

## 5. actor bound session push

```cpp
class player_actor_t final {
public:
    explicit player_actor_t(zlink::framework::actor_context_t &context)
      : context_(context)
    {
    }

    void notify_turn_changed(turn_changed_t event)
    {
        context_.bound_session().send(event).submit();
    }

private:
    zlink::framework::actor_context_t &context_;
};
```

actor에서 client로 push할 때도 application route mesh channel을 직접 만들지 않는다.
framework runtime의 bound session 경로를 사용한다.
