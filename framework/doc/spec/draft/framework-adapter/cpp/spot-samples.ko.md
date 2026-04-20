[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [SPOT](./cpp-spot.ko.md)

# Draft -- ZLink Framework C++ SPOT Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `SPOT` 초안을 샘플로 보기 위한 문서다.

```cpp
class stage_request_handler_t final : public spot_request_handler_t {
public:
    message_t handle(
      const message_t &request,
      const spot_request_context_t &context) override
    {
        return build_stage_reply(context.self().spot_rid(), 10);
    }
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
