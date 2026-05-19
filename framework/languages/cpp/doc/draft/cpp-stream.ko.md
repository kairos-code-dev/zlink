<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ SPOT](./cpp-spot.ko.md) | [다음: Draft -- ZLink Framework C++ Interface Alignment](./handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM open items](./stream-open-items.ko.md)

# Draft -- ZLink Framework C++ STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 1. 방향

framework MVP에서 `STREAM`은 packet 방식만 지원한다. 그중에서도 Header는 framework가
정의한 `stream_header_t` 방식만 지원한다.

따라서 아래 표면은 MVP에 넣지 않는다.

- raw stream session
- 사용자 정의 Header framing
- 임의 byte stream dispatch
- application이 직접 raw `stream_socket` recv loop를 돌리는 모델

이 제한은 framework가 packet lifecycle, dispatch ordering, backpressure, 오류 처리를
한곳에서 관리하기 위한 것이다. application은 Header와 payload를 받은 뒤 업무 처리에만
집중한다.

## 2. Session 표면

```cpp
namespace zlink::framework {

class stream_header_t {
public:
    std::string_view session_id() const;
    std::string_view packet_name() const;
    std::string_view content_type() const;
    std::optional<std::string_view> correlation_id() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual std::future<void> write_packet(
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual void on_connected(stream_t &stream) = 0;
    virtual void on_disconnected(stream_t &stream) = 0;
    virtual void on_packet(
      stream_t &stream,
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

} // namespace zlink::framework
```

`stream_header_t`는 framework가 만든 Header view다. 사용자가 임의 multipart header를
직접 파싱하거나 다른 header serializer를 선택하는 방식은 MVP 범위에 넣지 않는다.

## 3. Host 등록

STREAM endpoint와 packet session은 `use_zlink(...)`와 `app.handlers()`에서 구성한다.

```cpp
app.use_zlink([](auto &zlink) {
    zlink.node("stream-node")
      .stream("route-stream", [](auto &stream) {
          stream.bind("tcp://0.0.0.0:9200");
          stream.packet_session("route");
      });
});

app.handlers()
  .packet_stream(
    "route-stream",
    [](auto &stream, const auto &header, const auto &payload) {
        handle_route_packet(stream, header, payload);
    });
```

## 4. Dispatch 기준

- framework host가 binding의 `zlink::stream_socket_t` lifecycle을 관리한다.
- packet callback은 transport callback 안에서 직접 실행하지 않고 framework executor로
  넘긴다.
- 같은 stream session의 lifecycle callback과 packet callback은 직렬로 처리한다.
- Header 검증에 실패한 packet은 application handler로 넘기지 않는다.
- `stream_t::write_packet(...)`은 async submit으로 본다. backpressure는 pending queue와
  ready notification으로 처리한다.
