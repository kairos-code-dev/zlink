<!-- framework-adapter-nav:start -->
[문서 목록](../../../../README.ko.md) | [이전: C++ Guide](../README.ko.md) | [다음: C++ Channel Messaging Spec](../../../common/spec/languages/cpp/cpp-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../common/README.ko.md)

[C++ 묶음](../../README.ko.md) | [Framework 인터페이스](../../../common/spec/languages/cpp/cpp-framework-interfaces.ko.md) | [channel](../../../common/spec/languages/cpp/cpp-channel-messaging.ko.md)

# ZLink Framework C++ Channel Messaging Samples

이 문서는 현재 C++ public API를 사용하는 channel 등록, handler와 outbound 호출
예제를 설명한다. 공개 계약은 [channel spec](../../../common/spec/languages/cpp/cpp-channel-messaging.ko.md)이
소유한다.

## 1. Host 와 handler

```cpp
class user_handler_t final {
public:
    explicit user_handler_t(zlink::framework::request_client_t &client)
      : client_(client)
    {
    }

    zlink::framework::task_t<get_user_reply_t> get_user(
      const get_user_request_t &request)
    {
        auto account = co_await client_
          .request("account", account_query_t{
              .account_id = request.account_id,
          })
          .async<account_reply_t>();

        co_return build_user_reply(account);
    }

private:
    zlink::framework::request_client_t &client_;
};

auto app = zlink::framework::app_t::create();

app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.add_client_server_channel("api")
      .enable_server("tcp://0.0.0.0:7100")
      .use_handler_group("api");
    options.add_client_server_channel("account")
      .enable_client();
    options.handlers()
      .group ("api")
      .add<user_handler_t> ();
});
```

## 2. Outbound-only

```cpp
auto app = zlink::framework::app_t::create();

app.add_zlink_framework([](auto &options) {
    options.use_discovery().add_registry_endpoint ("tcp://registry:5551");
    options.add_client_server_channel("profile")
      .enable_client();
});
```

이 경우 local server 역할은 열지 않는다.

## 3. 수동 연결

```cpp
app.add_zlink_framework([](auto &options) {
    options.add_client_server_channel("profile")
      .enable_client("tcp://10.0.10.15:7101")
      .enable_client("tcp://10.0.10.16:7101");
});
```

`client(endpoint)`를 여러 번 호출하면 같은 client 역할에 manual endpoint를 추가한다.
수동 연결과 Discovery 연결은 같은 역할 안에서 섞지 않는다. endpoint 인자 없는
`client()`는 discovery mode로 전환하며, 같은 builder에서 앞서 추가한 manual endpoint를
사용하지 않는다.

## 4. 일반 event publish

```cpp
publisher.publish(
  "profile",
  "profile.cache-refreshed",
  build_profile_cache_refreshed(account_id),
  zlink::framework::send_options_t{
    .packet_name = "profile.cache-refreshed",
  });
```

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../../README.ko.md) | [이전: C++ Guide](../README.ko.md) | [다음: C++ Channel Messaging Spec](../../../common/spec/languages/cpp/cpp-channel-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->
