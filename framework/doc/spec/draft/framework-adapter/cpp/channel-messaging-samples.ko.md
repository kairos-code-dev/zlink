[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [channel](./cpp-channel-messaging.ko.md)

# Draft -- ZLink Framework C++ Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` channel messaging 초안을 샘플로 보기 위한 문서다.

## 1. Host 와 handler

```cpp
class user_handler_t final : public zlink::framework::request_handler_t {
public:
    explicit user_handler_t(zlink::framework::client_t &client)
      : client_(client) {}

    message_t handle(
      const message_t &request,
      const request_context_t &context) override
    {
        auto account = client_.request("account", request);
        return build_user_reply(account);
    }

private:
    zlink::framework::client_t &client_;
};

auto app = app_t::build()
  .set_channel_name("api")
  .add_outbound_channel("account")
  .use_discovery(...)
  .add_request_handler("GetUserRequest", user_handler);
```

## 2. Outbound-only

```cpp
auto app = app_t::build()
  .add_outbound_channel("profile")
  .use_discovery(...);
```

이 경우 local `ROUTER(server)`는 열지 않는다.
