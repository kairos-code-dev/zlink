[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [SPOT](./cpp-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `SPOT` 초안을 샘플로 보기 위한 문서다.

## 1. 등록과 `spot_name`

```cpp
auto app = app_t::build()
  .use_spot_discovery("game.stage", discovery_config_t{
      .registry_endpoints = {"tcp://registry1:5551"},
  })
  .add_spot_node("stage-node", [](auto &spot) {
      spot.bind("tcp://0.0.0.0:9000");
      spot.enable_router();
      spot.enable_pub_sub();
      spot.attach_channel_client("profile");
      spot.attach_spot_publisher_client("game.stage");
      spot.add_spot_factory("stage", "stage_spot_t");
      spot.add_spot_factory("room", "room_spot_t");
  });
```

## 2. manager로 생성과 조회

```cpp
class stage_bootstrap_t {
public:
    explicit stage_bootstrap_t(spot_manager_t &spot_manager)
      : spot_manager_(spot_manager) {}

    void warmup()
    {
        auto created = spot_manager_.create("stage");
        auto info = spot_manager_.get(created.spot_rid);
        auto all = spot_manager_.list();
    }

private:
    spot_manager_t &spot_manager_;
};
```

## 3. spot 객체와 timer

```cpp
class stage_spot_t final : public spot_t {
public:
    explicit stage_spot_t(routing_id_t spot_rid)
      : spot_rid_(spot_rid) {}

    routing_id_t spot_rid() const override
    {
        return spot_rid_;
    }

    std::unique_ptr<timer_t> add_timer(
      std::string name,
      std::chrono::milliseconds period,
      std::string handler_type_name) override;

    void initialize()
    {
        heartbeat_ = add_timer(
          "heartbeat",
          std::chrono::seconds(1),
          "stage_heartbeat_handler_t");
    }

private:
    routing_id_t spot_rid_;
    std::unique_ptr<timer_t> heartbeat_;
};
```

## 4. request, subscription, channel 호출

```cpp
class stage_request_handler_t final : public spot_request_handler_t {
public:
    explicit stage_request_handler_t(spot_client_t &spot_client)
      : spot_client_(spot_client) {}

    message_t handle(
      const message_t &request,
      const spot_request_context_t &context) override
    {
        auto profile = spot_client_.request_channel("profile", request);
        return build_stage_reply(context.self().spot_rid(), profile);
    }

private:
    spot_client_t &spot_client_;
};

class stage_subscription_handler_t final : public spot_subscription_handler_t {
public:
    void handle(
      const message_t &event,
      const spot_subscription_context_t &context) override
    {
    }
};
```

다른 channel 호출은 `spot_client.request_channel("profile", ...)` 같은 표면으로
설명하는 편이 맞다.

## 5. 외부 노드에서 `SPOT` publish

```cpp
spot_publisher_client.publish(
  "game.stage",
  "stage.state.updated",
  build_stage_state_updated()).get();
```
