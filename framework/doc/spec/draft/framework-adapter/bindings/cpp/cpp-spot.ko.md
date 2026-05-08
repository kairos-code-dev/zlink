[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [SPOT 샘플](./spot-samples.ko.md) | [Stage wrapper](./stage-wrapper-on-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 `SPOT`을 어떤 표면으로 통합할지
> 정리한다.

## 1. 방향

`SPOT`은 lightweight distributed endpoint다. actor와 비슷하지만 네트워크, 라우팅,
분산 runtime을 먼저 고려한다.

`C++` framework는 binding의 `zlink::service::spot_node_t`와
`zlink::service::spot_t`를 직접 노출하지 않고, 아래 표면으로 감싼다.

- `spot_node_builder_t`
- `spot_context_t`
- `send_ready_context_t`
- `module_t` 또는 DI 기반 spot owner 등록

일반 application handler는 channel/topic 중심으로 시작한다. 직접 `routing_id_t`를
받는 API는 spot-to-spot send/request 경로에 제한한다.

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
          spot_node.use_discovery("game.stage");
          spot_node.attach_channel_client("profile");
          spot_node.attach_publisher("game.stage");
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

    std::future<profile_reply_t> load_profile(profile_query_t query)
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

## 4. Backpressure

SPOT send path는 send-ready callback과 pending queue를 통해 backpressure를 드러낸다.

```cpp
context.on_send_ready([](zlink::framework::send_ready_context_t &ready) {
    ready.resume_pending();
});
```

기본 정책은 무한 queue가 아니다. queue 상한, submit timeout, overflow 정책은
framework runtime 설정으로 닫는다.

## 5. 중요한 규칙

- SPOT discovery 설정은 channel view 이름을 명시한다.
- spot-to-spot send/request 외의 일반 흐름은 channel name과 topic을 먼저 사용한다.
- SPOT node lifecycle은 app host가 관리한다.
- handler callback은 transport callback 안에서 직접 실행하지 않고 framework executor로
  넘긴다.
- 같은 SPOT node 안의 mailbox ordering과 handler concurrency 정책은 framework
  concurrency 계층에서 제어한다.
