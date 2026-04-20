[스펙 목차](../../../README.ko.md)

[C++ 묶음](./README.ko.md) | [channel](./cpp-channel-messaging.ko.md) | [SPOT](./cpp-spot.ko.md) | [STREAM](./cpp-stream.ko.md) | [Registry](./cpp-registry.ko.md)

# Draft -- ZLink Framework C++ Interface Catalog

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `C++` host/runtime이 노출할 공용 타입과 registration
> 표면을 한 곳에 모은 기준 문서다.

## 1. Host 와 Context

```cpp
namespace zlink::framework {

struct send_options_t {
    std::optional<std::string> packet_name;
};

struct request_options_t {
    std::optional<std::string> packet_name;
    std::optional<std::chrono::milliseconds> timeout;
};

struct handler_context_t {
    std::optional<std::string> channel_name;
    std::optional<std::string> packet_name;
    std::optional<std::string> content_type;
    std::optional<std::string> correlation_id;
};

} // namespace zlink::framework
```

## 2. Handler

```cpp
namespace zlink::framework {

class request_handler_t {
public:
    virtual ~request_handler_t() = default;
    virtual message_t handle(
      const message_t &request,
      const request_context_t &context) = 0;
};

class send_handler_t {
public:
    virtual ~send_handler_t() = default;
    virtual void handle(
      const message_t &message,
      const send_context_t &context) = 0;
};

class event_handler_t {
public:
    virtual ~event_handler_t() = default;
    virtual void handle(
      const message_t &event,
      const event_context_t &context) = 0;
};

} // namespace zlink::framework
```

## 3. Client

```cpp
namespace zlink::framework {

class client_t {
public:
    virtual ~client_t() = default;

    virtual void send(
      std::string_view channel_name,
      const message_t &message,
      const send_options_t &options = {}) = 0;

    virtual message_t request(
      std::string_view channel_name,
      const message_t &request,
      const request_options_t &options = {}) = 0;
};

} // namespace zlink::framework
```

packet key 해석 규칙은 아래 순서를 기본으로 본다.

1. `options.packet_name`
2. payload registration metadata
3. payload 타입 이름

## 4. Host

```cpp
namespace zlink::framework {

class app_t {
public:
    static app_t build();

    app_t &set_channel_name(std::string channel_name);
    app_t &add_outbound_channel(std::string channel_name);
    app_t &use_discovery(discovery_config_t config);
    app_t &use_manual_connections(manual_connections_t config);
    app_t &add_request_handler(std::string packet_name, request_handler_t &handler);
    app_t &add_send_handler(std::string packet_name, send_handler_t &handler);
    app_t &run();
};

} // namespace zlink::framework
```

## 5. 중요한 규칙

- 같은 outbound channel은 자동 연결과 수동 연결 중 하나만 선택한다.
- 일반 channel messaging의 handler dispatch는 local `ROUTER(server)` ingress 기준이다.
- outbound `DEALER(client)` 수신은 reply correlation 경로로 본다.
- `ROUTER -> DEALER` 임의 push는 channel messaging 공용 계약에 넣지 않는다.
