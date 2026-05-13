<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: ZLink Framework C++ SPOT Samples](spot-samples.ko.md) | [다음: ZLink Framework For Go](../go/README.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [STREAM](./cpp-stream.ko.md)

# Draft -- ZLink Framework C++ STREAM Samples

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` `STREAM` 초안을 샘플로 보기 위한 문서다.

## 1. Packet session 등록

```cpp
auto app = zlink::framework::app_t::create();

app.use_zlink([](auto &zlink) {
    zlink.node("route-node")
      .stream("route-stream", [](auto &stream) {
          stream.bind("tcp://0.0.0.0:9200");
          stream.packet_session("route");
      });
});
```

## 2. Packet handler

```cpp
app.handlers()
  .packet_stream(
    "route-stream",
    [](zlink::framework::stream_t &stream,
       const zlink::framework::stream_header_t &header,
       const zlink::message_t &body) {
        route_packet_t packet{
          .session_id = std::string(header.session_id()),
          .packet_name = std::string(header.packet_name()),
          .body = decode_route_body(body),
        };

        handle_route_packet(stream, packet);
    });
```

## 3. Packet reply

```cpp
std::future<void> send_route_ack(
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
      encode_route_ack(ack));
}
```

framework MVP는 raw stream session 샘플을 제공하지 않는다. Header도 framework가
정의한 `stream_header_t` 방식만 사용한다.
