<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ SPOT](cpp-spot.ko.md) | [다음: Spec -- ZLink Framework C++ Interface Alignment](handler-interfaces.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../common/README.ko.md)

[C++ 묶음](../README.ko.md) | [C++ 정책](../internals/cpp-framework-policy.ko.md) | [Framework 인터페이스](cpp-framework-interfaces.ko.md) | [STREAM 샘플](../internals/stream-samples.ko.md) | [STREAM decisions](../internals/stream-open-items.ko.md)

# Spec -- ZLink Framework C++ STREAM

> 이 문서는 **구현 완료된 설계 계약**이다.
> `C++` runtime에서 `STREAM`을 어떤 표면으로 올릴지
> 정리한다.

## 인터페이스 경계

STREAM public contract는 `contracts/streams/*`가 소유한다. public 표면에는
`stream_builder_t`, session contract, packet handler, bound session, stream call object,
metadata와 error model을 둔다. stream socket owner, frame codec, session table, request
tracker, session serial executor, transport loop는 `src/runtime/streams/*`에 둔다.

STREAM public API는 raw byte stream이 아니라 packet 모델이다. Header parsing과
backpressure 처리는 runtime 책임이며, application handler는 dispatch context, metadata,
payload만 다룬다.

## 1. 방향

framework core에서 `STREAM`은 packet 방식만 지원한다. 내부 wire header는 runtime이
소유하고, public session code는 `stream_dispatch_context_t`로 packet name과 metadata만 읽는다.

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
class request_call_t;

enum class stream_codec_t : std::uint8_t {
    raw = 0,
    json = 1,
    message_pack = 2,
    protobuf = 3
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

class stream_dispatch_context_t {
public:
    std::string_view packet_name() const;
    const stream_metadata_t &metadata() const;
    bool can_reply() const;
};

class stream_t {
public:
    virtual ~stream_t() = default;
    virtual std::string session_id() const = 0;
    virtual task_t<void> close() = 0;
    virtual stream_write_call_t write_packet(zlink::message_t payload) = 0;
    virtual stream_write_call_t reply_packet(zlink::message_t payload) = 0;
};

class stream_write_call_t {
public:
    stream_write_call_t &metadata(std::string key, std::string value);
    stream_write_call_t &packet_name(std::string packet_name);
    stream_write_call_t &compress();
    task_t<void> async();
};

class session_actor_t {
public:
    relay_call_t relay(const zlink::message_t &payload);
};

class session_actor_manager_t {
public:
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    std::optional<session_actor_t> find(std::string actor_id) const;
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id);
    result_t<session_actor_t> get_or_create(
        std::string actor_type,
        std::string actor_id,
        message_t create_request);
    request_call_t<session_actor_t> bind(actor_ref_t actor);
};

class packet_stream_session_t {
public:
    virtual ~packet_stream_session_t() = default;
    virtual task_t<void> on_connected(stream_t &stream) = 0;
    virtual task_t<void> on_disconnected(stream_t &stream) = 0;
    virtual task_t<void> on_error(stream_t &stream, const stream_error_t &error) = 0;
    virtual task_t<void> on_packet(
      stream_t &stream,
      const stream_dispatch_context_t &dispatch,
      const zlink::message_t &payload);
};

} // namespace zlink::framework
```

업무 session은 header 객체를 받거나 되돌려 주지 않는다. packet name과 metadata는
`stream_dispatch_context_t`에서 읽고, reply와 actor relay에 필요한 request sequence와
correlation 값은 runtime이 내부 dispatch state로 보존한다. C++ stream session payload는
`zlink::message_t` 하나로 유지하며, 별도 `_raw` 이름의 public API를 두지 않는다.

## 3. Host 등록

STREAM endpoint와 packet session은 `add_zlink_framework(...)` 안의 options builder에서
구성한다. packet 처리는 `packet_stream_session_t` 구현체의 `on_packet(...)`에서 담당한다.

```cpp
class client_session_t : public zlink::framework::packet_stream_session_t {
public:
    static constexpr const char *session_name = "client";

    // on_connected/on_packet/on_disconnected/on_error 구현
};

app.add_zlink_framework([](auto &options) {
    options.add_spot_mesh("session-actors")
      .bind("tcp://0.0.0.0:7101");
    options.add_stream_node("route-stream")
      .bind("tcp://0.0.0.0:9200")
      .register_session<client_session_t>()
});
```

`register_session<TSession>()`은 `.NET`의 `RegisterSession<TSession>()`에 해당한다.
`TSession`은 `packet_stream_session_t`를 상속해야 하며 framework service collection에
stream-session scope 서비스로 등록된다. `TSession::session_name`이 있으면 그 값을 native packet
session 이름으로 사용하고, 없으면 타입 이름 기반 message name을 사용한다. low-level
`register_session(name)`은 session 이름을 직접 지정해야 하는 경우에만 사용한다.
하나의 stream node에는 packet session을 하나만 선언한다. 같은 stream node에서
`register_session<T>()`과 `register_session(...)`을 중복 호출하면 설정 오류로 처리한다.

## 4. Dispatch 기준

- framework host가 binding의 `zlink::stream_socket_t` lifecycle을 관리한다.
- packet callback은 framework가 packet 수신과 header 검증을 마친 뒤 호출한다. 별도
  실행기로 넘기는 것이 기본은 아니다.
- CPU-bound 또는 blocking 가능성이 있는 stream handler는 offload 실행 정책을 명시한다.
- 같은 stream session의 lifecycle callback과 packet callback은 직렬로 처리한다.
- Header 검증에 실패한 packet은 application handler로 넘기지 않는다.
- `stream_t::write_packet(...)`과 `reply_packet(...)`은 async submit으로 본다. 반환된
  `stream_write_call_t`에서 `metadata(...)`, `packet_name(...)`, `compress()`를 설정할 수 있고,
  실제 실행은 `async()`에서 시작한다.
- `stream_t::close()`는 session을 닫고 이후 write submit이 `disconnected` 결과를 반환하게
  한다. 이미 닫힌 stream을 다시 닫는 것은 성공으로 처리해 cleanup 호출자가 중복 close를
  특별히 구분하지 않아도 되게 한다.
- session actor dispatch는 STREAM session에서 route mesh channel로 직접 packet을 만들지
  않고, ActorGateway와 `session_actor_t::relay(...)`를 사용한다.
- session callback 동안 받은 `payload`는 framework가 빌려준 값이므로 relay 호출자가
  해제하거나 move로 소비하지 않는다.

## 5. 회귀 테스트

STREAM 회귀 테스트는 `.NET` framework의 packet session 동작과 같은 의미를 C++ 표면으로
고정한다. raw byte stream 성공 여부만 보지 않고 header validation, session lifecycle,
write backpressure, session relay 경계를 함께 검증한다.

필수 항목:

- stream endpoint는 app host lifecycle에 묶여 bind/start/stop된다.
- `on_connected`, `on_packet`, `on_disconnected`, `on_error` callback은 같은 session 안에서
  직렬로 호출된다.
- valid packet은 `stream_dispatch_context_t`와 payload로 handler에 전달된다.
- invalid header, unsupported codec, malformed metadata는 application handler에 전달되지
  않고 error log와 monitoring event를 남긴다.
- `stream_t::write_packet(...)`은 submit 전에는 실행되지 않고, submit 후 backpressure와
  completion result를 반환한다. metadata, packet name override, compression flag는 submit
  시점에 framework header로 반영된다.
- `stream_t::close()` 후 새 write submit은 `disconnected` 결과를 반환하고, 추가 frame을 쓰지
  않는다.
- pending write 중 disconnect가 발생하면 caller는 disconnected 계열 error를 받는다.
- session-scoped service는 disconnect cleanup 뒤 해제된다.
- session callback에서 받은 payload는 relay 후에도 framework ownership 규칙을 깨지 않는다.
  ActorGateway relay 경로를 사용한다.
- shutdown 중 새 session accept를 멈추고 기존 session cleanup을 완료한다.

CTest label은 `framework-zlink-stream`을 사용한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Spec -- ZLink Framework C++ SPOT](cpp-spot.ko.md) | [다음: Spec -- ZLink Framework C++ Interface Alignment](handler-interfaces.ko.md)
<!-- framework-adapter-nav:bottom:end -->
