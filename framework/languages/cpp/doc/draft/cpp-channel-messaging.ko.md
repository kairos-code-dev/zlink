<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ Channel Messaging Samples](./channel-messaging-samples.ko.md) | [다음: Draft -- ZLink Framework C++ Interface Design](./cpp-framework-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [channel 샘플](./channel-messaging-samples.ko.md)

# Draft -- ZLink Framework C++ Channel Messaging

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime에서 channel messaging을 어떤 표면으로
> 드러낼지 정리한다.

## 1. Channel 의미

channel은 framework에서 request/reply와 pub/sub capability를 묶는 이름이다.
사용자는 raw socket을 직접 만들지 않고, channel 이름과 capability 설정으로 runtime을
구성한다.

channel 자체는 연결 주체가 아니다. 실제 endpoint, Discovery, timeout 같은 연결 설정은
아래 capability builder에 둔다.

- server
- client
- publisher
- subscriber

이렇게 나누면 같은 `orders` channel 안에서도 request/reply와 pub/sub 연결 정책을
분리할 수 있다.

## 2. Host bootstrap

```cpp
using namespace zlink::framework;

auto app = app_t::create();

app.use_zlink([](auto &zlink) {
    zlink.node("api-node")
      .discovery([](auto &discovery) {
          discovery.connect_registry("tcp://registry1:5551");
          discovery.connect_registry("tcp://registry2:5551");
      })
      .channel("api", [](auto &channel) {
          channel.enable_server([](auto &server) {
              server.bind("tcp://0.0.0.0:7100");
          });
      })
      .channel("profile", [](auto &channel) {
          channel.enable_client([](auto &client) {
              client.use_discovery();
          });
      })
      .channel("account", [](auto &channel) {
          channel.enable_client([](auto &client) {
              client.use_discovery();
          });
      });
});
```

수동 연결은 아래처럼 둔다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("api-node")
      .channel("profile", [](auto &channel) {
          channel.enable_client([](auto &client) {
              client.connect("tcp://10.0.10.15:7101");
          });
      });
});
```

이 설정은 `profile` channel 전체가 아니라 `profile.client` 연결 집합에만 적용된다.
같은 `profile` channel이라도 `profile.subscriber`는 별도 연결 집합으로 본다.
같은 capability 안에서는 수동 연결과 Discovery 연결을 섞지 않는다.

## 3. Handler 등록

handler는 `app.handlers()` 아래 typed registry로 등록한다.

```cpp
app.services()
  .add_transient<user_handler_t>();

app.handlers()
  .request<get_user_request_t, get_user_reply_t, user_handler_t>(
    "api",
    "GetUserRequest",
    &user_handler_t::get_user);

app.handlers()
  .send<refresh_profile_cache_t, cache_handler_t>(
    "api",
    "RefreshProfileCacheCommand",
    &cache_handler_t::refresh);
```

기본 packet key는 payload 타입 이름에서 얻는다. 별도 이름이 필요하면
`handler_options_t::packet_name`이나 등록 인자를 사용한다.

일반 event publish는 `publisher_t::publish(channel, topic, event)` 표면으로 설명한다.

## 4. Dispatch 기준

- 일반 request/send dispatch는 local server capability ingress 기준이다.
- outbound client capability 수신은 pending request의 reply correlation 경로다.
- 같은 capability에서 Discovery와 manual 연결을 같이 섞지 않는다.
- runtime 연결 제어가 필요하면 capability 단위 connection manager를 별도 extension으로
  둔다.

## 5. Outbound-only host

local handler 없이 outbound client만 쓰는 host도 가능해야 한다.
이 경우 local server capability는 열지 않고 outbound client capability만 만든다.
