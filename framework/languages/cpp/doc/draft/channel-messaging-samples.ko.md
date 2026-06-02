<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework For C++](./README.ko.md) | [다음: Draft -- ZLink Framework C++ Channel Messaging](./cpp-channel-messaging.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [channel](./cpp-channel-messaging.ko.md)

# Draft -- ZLink Framework C++ Channel Messaging Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` channel messaging 초안을 샘플로 보기 위한 문서다.

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
          .request<account_reply_t>("account", account_query_t{
              .account_id = request.account_id,
          })
          .submit();

        co_return build_user_reply(account);
    }

private:
    zlink::framework::request_client_t &client_;
};

auto app = zlink::framework::app_t::create();

app.add_zlink_framework([](auto &options) {
    options.discovery().add("tcp://registry:5551");
    options.codecs().add_json();
    options.client_server_channel("api")
      .server("tcp://0.0.0.0:7100")
      .handler_group("api");
    options.client_server_channel("account")
      .client();
    options.handlers()
      .add<user_handler_t>("api");
});
```

## 2. Outbound-only

```cpp
auto app = zlink::framework::app_t::create();

app.add_zlink_framework([](auto &options) {
    options.discovery().add("tcp://registry:5551");
    options.client_server_channel("profile")
      .client();
});
```

이 경우 local server capability는 열지 않는다.

## 3. 수동 연결

```cpp
app.add_zlink_framework([](auto &options) {
    options.client_server_channel("profile")
      .client("tcp://10.0.10.15:7101");
});
```

수동 연결과 Discovery 연결은 같은 capability 안에서 섞지 않는다.

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
