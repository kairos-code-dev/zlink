<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: C++ Stream Connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](cpp-framework-policy.ko.md) | [Framework 인터페이스](../spec/cpp-framework-interfaces.ko.md) | [STREAM](../spec/cpp-stream.ko.md)

# Draft -- ZLink Framework C++ STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM` 초안을 샘플로 보기 위한 문서다.

## 1. Packet session 등록

```cpp
auto app = zlink::framework::app_t::create();

app.add_zlink_framework([](auto &options) {
    options.spot_node("session-actors")
      .bind("tcp://0.0.0.0:7101")
      .enable_actor_gateway();
    options.add_stream_node("route-stream")
      .bind("tcp://0.0.0.0:9200")
      .register_session<route_session_t>()
      .attach_actor_gateway("session-actors");
});
```

## 2. Packet session

```cpp
class route_session_t final : public zlink::framework::packet_stream_session_t {
public:
    zlink::framework::result_t<void> on_packet(
      zlink::framework::stream_t &stream,
      const zlink::framework::stream_header_t &header,
      const zlink::framework::message_t &payload) override
    {
        route_packet_t packet{
          .session_id = stream.session_id(),
          .packet_name = std::string(header.packet_name()),
          .payload = payload.decode<route_body_t>(),
        };

        handle_route_packet(stream, packet);
        return zlink::framework::result_t<void>::success();
    }
};
```

## 3. Packet reply

```cpp
zlink::framework::stream_write_call_t send_route_ack(
  zlink::framework::stream_t &stream,
  const zlink::framework::stream_header_t &request_header,
  const route_ack_t &ack)
{
    auto reply_header = make_framework_stream_header(
      "route.ack",
      "application/json",
      request_header.correlation_id());

    return stream.write_packet(
      reply_header,
      zlink::framework::message_t::from(ack));
}
```

framework core는 raw stream session 샘플을 제공하지 않는다. Header도 framework가
정의한 `stream_header_t` 방식만 사용한다. 기존처럼 `zlink::message_t`를 직접
만들어 `write_packet(...)`에 넘기던 예시는 일반 업무 경로에서 제거한다. raw payload를
그대로 보존해야 하는 runtime boundary나 test harness만 `write_packet_raw(...)` 또는
`reply_packet_raw(...)`를 사용한다.

## 4. actor relay

```cpp
class game_session_t final : public zlink::framework::packet_stream_session_t {
public:
    explicit game_session_t(zlink::framework::session_actor_manager_t &actors)
      : actors_(actors)
    {
    }

    zlink::framework::task_t<void> on_packet(zlink::framework::stream_t &stream,
      const zlink::framework::stream_header_t &header,
      const zlink::framework::message_t &payload) override
    {
        if (is_login(header)) {
            actor_ = co_await actors_
              .bind(find_actor_ref(payload))
              .async();
            co_return;
        }

        co_await actor_
          .relay(header, payload)
          .async();
    }

private:
    zlink::framework::session_actor_manager_t &actors_;
    zlink::framework::session_actor_t actor_;
};
```

session actor relay는 route mesh packet을 application 코드에서 만들지 않는다. STREAM
session은 attach된 ActorGateway를 통해 actor로 packet을 넘긴다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: C++ Stream Connector 가이드](../../../stream-connector/cpp/guide/INDEX.ko.md)
<!-- framework-adapter-nav:bottom:end -->
