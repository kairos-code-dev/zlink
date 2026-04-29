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
  .add_channel("api", [](auto &channel) {
      channel.enable_server();
  })
  .add_channel("account", [](auto &channel) {
      channel.enable_client();
  })
  .use_discovery(...)
  .add_request_handler("GetUserRequest", user_handler);
```

## 2. Outbound-only

```cpp
auto app = app_t::build()
  .add_channel("profile", [](auto &channel) {
      channel.enable_client();
  })
  .use_discovery(...);
```

이 경우 local `ROUTER(server)`는 열지 않는다.

## 3. 수동 연결과 런타임 제어

```cpp
app.add_channel("profile", [](auto &channel) {
  channel.enable_client([](auto &client) {
    client.use_manual_connections({
      "tcp://10.0.10.15:7101",
    });
  });
});
```

```cpp
auto &profile_client = connection_manager.get_client("profile");
profile_client.connect("tcp://10.0.10.17:7101");
```

## 4. 일반 event publish

```cpp
event_publisher.publish(
  "profile",
  "profile.cache-refreshed",
  build_profile_cache_refreshed(account_id),
  send_options_t{
    .packet_name = "profile.cache-refreshed",
  }).get();
```
