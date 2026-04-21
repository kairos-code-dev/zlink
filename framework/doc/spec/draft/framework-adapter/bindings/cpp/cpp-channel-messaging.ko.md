[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [인터페이스](./handler-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md)

# Draft -- ZLink Framework C++ Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 1. Host bootstrap

```cpp
using namespace zlink::framework;

auto app = app_t::build()
  .add_channel("api", [](auto &channel) {
      channel.enable_server();
  })
  .add_channel("profile", [](auto &channel) {
      channel.enable_client();
  })
  .add_channel("account", [](auto &channel) {
      channel.enable_client();
  })
  .use_discovery(discovery_config_t{
      .registry_endpoints = {
        "tcp://registry1:5551",
        "tcp://registry2:5551",
      },
  });
```

수동 연결은 아래처럼 둔다.

```cpp
app.add_channel("profile", [](auto &channel) {
  channel.enable_client([](auto &client) {
    client.use_manual_connections({
      "tcp://10.0.10.15:7101",
    });
  });
});
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.
같은 `profile` channel이라도 `profile.subscriber`는 별도 연결 집합으로 본다.
channel client manual 연결은 remote `routing_id_t`를 따로 받지 않고 endpoint
집합만 관리한다.

## 2. Handler 등록

```cpp
app.add_request_handler("GetUserRequest", user_handler);
app.add_send_handler("RefreshProfileCacheCommand", cache_handler);
```

기본 packet key는 payload 타입 이름을 쓰고, 별도 이름이 필요하면 registration
metadata 또는 options에서 override한다.

일반 `PUB/SUB` event publish는 `event_publisher_t` 같은 별도 surface로 설명하는
편이 맞다. 이 표면도 `channel_name + topic` 기준으로 동작한다.

## 3. Dispatch 기준

- 일반 request/send dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 pending request의 reply correlation 경로다.
- 같은 capability에서 discovery와 manual을 같이 섞지 않는다.
- manual capability는 런타임 `connect`, `disconnect`, `list_connections`도 지원해야 한다.

## 4. Outbound-only host

local handler 없이 outbound client만 쓰는 host도 가능해야 한다.
이 경우 local `ROUTER(server)`는 열지 않고 outbound `DEALER(client)`만 만든다.
