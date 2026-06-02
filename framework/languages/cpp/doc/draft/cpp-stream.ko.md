<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: Draft -- ZLink Framework C++ SPOT](./cpp-spot.ko.md) | [다음: Draft -- ZLink Framework C++ Interface Alignment](./handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](./README.ko.md) | [C++ 정책](./cpp-framework-policy.ko.md) | [Framework 인터페이스](./cpp-framework-interfaces.ko.md) | [STREAM 샘플](./stream-samples.ko.md) | [STREAM decisions](./stream-open-items.ko.md)

# Draft -- ZLink Framework C++ STREAM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` runtime에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 인터페이스 경계

STREAM public contract는 `contracts/streams/*`가 소유한다. public 표면에는
`stream_builder_t`, session contract, packet handler, bound session, stream call object,
metadata와 error model을 둔다. stream socket owner, frame codec, session table, request
tracker, session serial executor, transport loop는 `src/runtime/streams/*`에 둔다.

STREAM public API는 raw byte stream이 아니라 framework Header 기반 packet 모델이다.
Header parsing과 backpressure 처리는 runtime 책임이며, application handler는 이미 정리된
packet, metadata, typed payload만 다룬다.

## 1. 방향

framework core에서 `STREAM`은 packet 방식만 지원한다. 그중에서도 Header는 framework가
정의한 `stream_header_t` 방식만 지원한다.

따라서 아래 표면은 core public 표면에 넣지 않는다.

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

class actor_ref_t;
class relay_call_t;
class stream_write_call_t;
template <typename T>
class task_t;
template <typename TActor>
class bind_actor_call_t;

enum class stream_message_kind_t : std::uint8_t {
    send = 1,
    request = 2,
    response = 3,
    error = 4,
    control = 5
};

enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
};

enum class stream_header_flags_t : std::uint8_t {
    none = 0,
    has_request_seq = 0x01,
    has_metadata = 0x02,
    payload_compressed = 0x04
};

enum class stream_session_error_t {
    internal,
    transport_error,
    handshake_failed
};

class stream_error_t {
public:
    stream_session_error_t error() const;
    int native_code() const;
    std::string_view message() const;
};

class stream_header_t {
public:
    stream_message_kind_t kind() const;
    stream_codec_t codec() const;
    stream_header_flags_t flags() const;
    std::optional<std::uint64_t> request_seq() const;
    std::string_view packet_name() const;
    std::optional<std::string_view> metadata(std::string_view key) const;
    std::optional<std::string_view> correlation_id() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual stream_write_call_t write_packet(
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

class session_actor_t {
public:
    relay_call_t relay(
      const stream_header_t &header,
      const zlink::message_t &payload);
};

class session_actor_manager_t {
public:
    bind_actor_call_t<session_actor_t> bind(actor_ref_t actor);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_header_t &header,
      const zlink::message_t &payload) = 0;
};

} // namespace zlink::framework
```

`stream_header_t`는 framework가 만든 Header view다. wire 의미는 `.NET`의
`ZlinkStreamHeader`와 맞추며, kind, codec, flags, request sequence, packet name,
metadata를 담는다. 사용자가 임의 multipart header를 직접 파싱하거나 다른 header
serializer를 선택하는 방식은 core public 표면에 넣지 않는다.

## 3. Host 등록

STREAM endpoint와 packet session은 `add_zlink_framework(...)` 안의 options builder에서
구성한다. packet 처리는 `packet_stream_session_t` 구현체의 `on_packet(...)`에서 담당한다.

```cpp
app.add_zlink_framework([](auto &options) {
    options.spot_node("session-actors")
      .bind("tcp://0.0.0.0:7101")
      .enable_actor_gateway();
    options.stream_node("route-stream")
      .bind("tcp://0.0.0.0:9200")
      .packet_session("route")
      .attach_actor_gateway("session-actors");
});
```

## 4. Dispatch 기준

- framework host가 binding의 `zlink::stream_socket_t` lifecycle을 관리한다.
- packet callback은 framework가 packet 수신과 header 검증을 마친 뒤 호출한다. 별도
  실행기로 넘기는 것이 기본은 아니다.
- CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행 정책을 명시한다.
- 같은 stream session의 lifecycle callback과 packet callback은 직렬로 처리한다.
- Header 검증에 실패한 packet은 application handler로 넘기지 않는다.
- `stream_t::write_packet(...)`은 async submit으로 본다. backpressure는 pending queue와
  ready notification으로 처리하며, 실제 실행은 반환된 call object의 `submit()`에서
  시작한다.
- session actor dispatch는 STREAM session에서 route mesh channel로 직접 packet을 만들지
  않고, attach된 ActorGateway와 `session_actor_t::relay(...)`를 사용한다.
- session callback 동안 받은 `payload`는 framework가 빌려준 값이므로 relay 호출자가
  해제하거나 move로 소비하지 않는다.

## 5. 회귀 테스트

STREAM 회귀 테스트는 `.NET` framework의 packet session 동작과 같은 의미를 C++ 표면으로
고정한다. raw byte stream 성공 여부만 보지 않고 header validation, session lifecycle,
write backpressure, ActorGateway attach 경계를 함께 검증한다.

필수 항목:

- stream endpoint는 app host lifecycle에 묶여 bind/start/stop된다.
- `on_connected`, `on_packet`, `on_disconnected`, `on_error` callback은 같은 session 안에서
  직렬로 호출된다.
- valid packet은 `stream_header_t`와 payload로 handler에 전달된다.
- invalid header, unsupported codec, malformed metadata는 application handler에 전달되지
  않고 error log와 monitoring event를 남긴다.
- `stream_t::write_packet(...)`은 submit 전에는 실행되지 않고, submit 후 backpressure와
  completion result를 반환한다.
- pending write 중 disconnect가 발생하면 caller는 disconnected 계열 error를 받는다.
- session-scoped service는 disconnect cleanup 뒤 해제된다.
- session callback에서 받은 payload는 relay 후에도 framework ownership 규칙을 깨지 않는다.
- `attach_actor_gateway(...)`가 설정된 stream은 route mesh channel을 만들지 않고
  ActorGateway relay 경로를 사용한다.
- shutdown 중 새 session accept를 멈추고 기존 session cleanup을 완료한다.

CTest label은 `framework-zlink-stream`을 사용한다.
