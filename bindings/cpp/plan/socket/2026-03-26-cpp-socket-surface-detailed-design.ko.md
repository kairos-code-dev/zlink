# C++ Socket Surface 상세 설계

작성일: 2026-03-26

## 1. 목적

이 문서는 `bindings/cpp`의 socket 계층 public surface를 재정의한다.

목표는 두 가지다.

- 사용자가 socket 타입별로 "무엇을 할 수 있는지"를 쉽게 이해하게 만들 것
- 내부 구현은 가능한 한 하나의 깊은 공통 모듈에 모아 change amplification을 줄일 것

즉, 타입별 클래스를 늘리되 구현을 흩뿌리는 방식이 아니라, 공통 구현은 한곳에
두고 타입별 facade가 허용된 API만 열어주는 구조를 채택한다.

## 2. 설계 원칙

- socket 타입별 클래스는 "새 구현체"가 아니라 "제한된 facade"다.
- lifecycle, native handle ownership, option 공통 처리, monitor 연결, callback 등록
  같은 공통 메커니즘은 전부 `base_socket_t`에 둔다.
- raw transport 계층과 topic/service 계층을 public API에서 분리한다.
- `send/recv`와 `publish/subscribe`는 같은 이름 체계로 섞지 않는다.
- `message_t`는 payload container이고, payload 변환 책임도 `message_t`가 가진다.
- 각 socket 타입은 자기 타입에서만 의미 있는 option API만 노출한다.
- unsupported 동작은 runtime 오류보다 compile-time surface 제한을 우선한다.
- 구형 `setsockopt/getsockopt` 스타일 이름은 public surface에 노출하지 않는다.

## 3. 최종 계층 구조

```text
socket_handle_t
  ^
  |
base_socket_t
  ^
  +-- send_socket_t
  |     +-- push_socket_t
  |     +-- scatter_socket_t
  |
  +-- recv_socket_t
  |     +-- pull_socket_t
  |     +-- gather_socket_t
  |
  +-- message_socket_t
  |     +-- pair_socket_t
  |     +-- req_socket_t
  |     +-- rep_socket_t
  |     +-- dealer_socket_t
  |     +-- router_socket_t
  |     +-- stream_socket_t
  |
  +-- publisher_socket_t
  |     +-- pub_socket_t
  |     +-- xpub_socket_t
  |
  +-- subscriber_socket_t
        +-- sub_socket_t
        +-- xsub_socket_t
```

정책:

- `socket_handle_t`는 최소 ownership wrapper다.
- `base_socket_t`는 공통 동작을 제공하는 실제 깊은 모듈이다.
- 방향성 제약은 `send_socket_t`, `recv_socket_t`, `message_socket_t`에서 해결한다.
- 나머지 타입별 클래스는 public surface 제한과 타입별 option 노출만 담당한다.

## 4. 네임스페이스와 헤더 배치

최종 헤더 배치는 아래로 고정한다.

- `include/zlink/socket_handle.hpp`
- `include/zlink/base_socket.hpp`
- `include/zlink/send_socket.hpp`
- `include/zlink/recv_socket.hpp`
- `include/zlink/message_socket.hpp`
- `include/zlink/publisher_socket.hpp`
- `include/zlink/subscriber_socket.hpp`
- `include/zlink/socket_types.hpp`

`socket_types.hpp`에는 구체 타입 facade를 모은다.

- `pair_socket_t`
- `push_socket_t`
- `pull_socket_t`
- `req_socket_t`
- `rep_socket_t`
- `dealer_socket_t`
- `router_socket_t`
- `stream_socket_t`
- `pub_socket_t`
- `sub_socket_t`
- `xpub_socket_t`
- `xsub_socket_t`
- `scatter_socket_t`
- `gather_socket_t`

`include/zlink.hpp`는 위 헤더를 최종 public umbrella에 포함한다.

추가 정책:

- 기존 [`socket.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/socket.hpp)
  는 즉시 삭제하지 않는다.
- `socket.hpp`는 새 socket 계층 헤더를 재노출하는 umbrella/compat header로 축소한다.
- 새 샘플과 새 contract test는 `socket_t` generic 생성자를 사용하지 않는다.

## 5. 클래스별 책임

### 5.1 `socket_handle_t`

역할:

- raw native socket handle 소유
- move-only ownership
- `close()`와 `handle()`만 제공

이 클래스는 low-level ownership utility이며, 일반 사용자가 직접 쓸 주 surface가
아니다.

고정 인터페이스:

```cpp
class socket_handle_t {
public:
    socket_handle_t() noexcept;
    explicit socket_handle_t(void *socket, bool own = true) noexcept;
    ~socket_handle_t();

    socket_handle_t(socket_handle_t&& other) noexcept;
    socket_handle_t& operator=(socket_handle_t&& other) noexcept;

    socket_handle_t(const socket_handle_t&) = delete;
    socket_handle_t& operator=(const socket_handle_t&) = delete;

    bool valid() const noexcept;
    void *handle() noexcept;
    const void *handle() const noexcept;

    int close() noexcept;

protected:
    static socket_handle_t adopt(void *socket) noexcept;
};
```

### 5.2 `base_socket_t`

역할:

- 공통 lifecycle API
- 공통 endpoint API
- 공통 option API
- router/dealer/pub/sub/stream option domain dispatch
- monitor/service monitor attach
- callback registration 공통 처리
- discovery attach 공통 처리

`base_socket_t`는 직접 생성되지 않는다. protected constructor로만 사용한다.

고정 인터페이스:

```cpp
class base_socket_t : public socket_handle_t {
public:
    bool valid() const noexcept;

    int bind(const std::string &endpoint);
    int connect(const std::string &endpoint);
    int unbind(const std::string &endpoint);
    int disconnect(const std::string &endpoint);

    int attach_discovery(service::discovery_t &discovery);

    int recv_handler(zlink_socket_msg_handler_fn handler,
                     void *userdata = NULL);
    int subscribe_handler(zlink_subscribe_handler_fn handler,
                          void *userdata = NULL);
    int send_ready_handler(zlink_send_ready_handler_fn handler,
                           void *userdata = NULL);

    monitor_handle_t monitor_handle(monitor_event events) const;
    service_monitor_handle_t service_monitor_handle(
      service_monitor_event events) const;

    int set_option(socket_option option, const void *value, size_t size);
    int get_option(socket_option option, void *value, size_t *size) const;

    template<typename T>
    int set_option(socket_option option, const T &value);

    template<typename T>
    int get_option(socket_option option, T *value) const;

    int set_option(socket_option_key_t<std::string> key,
                   const std::string &value);
    int get_option(socket_option_key_t<std::string> key,
                   std::string &value) const;

    template<typename T>
    int set_option(socket_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(socket_option_key_t<T> key, T *value) const;

protected:
    int set_router_option(router_option option, const void *value, size_t size);
    int get_router_option(router_option option, void *value, size_t *size) const;

    int set_dealer_option(dealer_option option, const void *value, size_t size);
    int get_dealer_option(dealer_option option, void *value, size_t *size) const;

    int set_pub_option(pub_option option, const void *value, size_t size);
    int get_pub_option(pub_option option, void *value, size_t *size) const;

    int set_sub_option(sub_option option, const void *value, size_t size);
    int get_sub_option(sub_option option, void *value, size_t *size) const;

    int set_stream_option(stream_option option, const void *value, size_t size);
    int get_stream_option(stream_option option,
                          void *value,
                          size_t *size) const;

    int set_routing_id(const void *data, size_t size);
    int set_routing_id(const std::string &routing_id);
    int get_routing_id(zlink_routing_id_t &routing_id) const;
    int get_routing_id(std::string &routing_id) const;

    int set_tls_server(const std::string &cert,
                       const std::string &key,
                       bool require_client_cert = false);
    int set_tls_client(const std::string &ca_cert,
                       const std::string &hostname,
                       bool trust_system = false);

    base_socket_t(context_t &ctx, socket_type type);
    explicit base_socket_t(void *socket, bool own = true) noexcept;
};
```

제약:

- `base_socket_t`에는 `send`, `recv`, `publish`, `subscribe`를 직접 public으로
  두지 않는다.
- data-plane 동작은 하위 facade가 의미에 맞게 노출한다.
- 타입별 option domain API는 `base_socket_t`에 구현하되 protected로 둔다.

### 5.3 `send_socket_t`

역할:

- 송신만 가능한 raw transport facade

대상 타입:

- `PUSH`
- `SCATTER`

고정 인터페이스:

```cpp
class send_socket_t : public base_socket_t {
public:
    int send(message_t &msg, send_flag flags = send_flag::none);
    int send(std::vector<message_t> &parts, send_flag flags = send_flag::none);

    int send(const zlink_routing_id_t &rid,
             message_t &msg,
             send_flag flags = send_flag::none);
    int send(const zlink_routing_id_t &rid,
             std::vector<message_t> &parts,
             send_flag flags = send_flag::none);

protected:
    send_socket_t(context_t &ctx, socket_type type);
};
```

### 5.4 `recv_socket_t`

역할:

- 수신만 가능한 raw transport facade

대상 타입:

- `PULL`
- `GATHER`

고정 인터페이스:

```cpp
class recv_socket_t : public base_socket_t {
public:
    int recv(message_t &msg, recv_flag flags = recv_flag::none);
    int recv(std::vector<message_t> &parts, recv_flag flags = recv_flag::none);

    int recv(zlink_routing_id_t &rid,
             message_t &msg,
             recv_flag flags = recv_flag::none);
    int recv(zlink_routing_id_t &rid,
             std::vector<message_t> &parts,
             recv_flag flags = recv_flag::none);

protected:
    recv_socket_t(context_t &ctx, socket_type type);
};
```

### 5.5 `message_socket_t`

역할:

- 양방향 raw transport facade

대상 타입:

- `PAIR`
- `REQ`
- `REP`
- `DEALER`
- `ROUTER`
- `STREAM`

고정 인터페이스:

```cpp
class message_socket_t : public base_socket_t {
public:
    int send(message_t &msg, send_flag flags = send_flag::none);
    int send(std::vector<message_t> &parts, send_flag flags = send_flag::none);

    int send(const zlink_routing_id_t &rid,
             message_t &msg,
             send_flag flags = send_flag::none);
    int send(const zlink_routing_id_t &rid,
             std::vector<message_t> &parts,
             send_flag flags = send_flag::none);

    int recv(message_t &msg, recv_flag flags = recv_flag::none);
    int recv(std::vector<message_t> &parts, recv_flag flags = recv_flag::none);

    int recv(zlink_routing_id_t &rid,
             message_t &msg,
             recv_flag flags = recv_flag::none);
    int recv(zlink_routing_id_t &rid,
             std::vector<message_t> &parts,
             recv_flag flags = recv_flag::none);

protected:
    message_socket_t(context_t &ctx, socket_type type);
};
```

정책:

- `send_socket_t`에는 `recv(...)`가 없다.
- `recv_socket_t`에는 `send(...)`가 없다.
- `message_socket_t`만 양방향 `send/recv`를 노출한다.
- 구현 helper는 공유해도 public surface는 방향별로 분리한다.

### 5.6 `publisher_socket_t`

역할:

- topic publish 계층 facade
- `publish`만 공개

대상 타입:

- `PUB`
- `XPUB`

고정 인터페이스:

```cpp
class publisher_socket_t : public base_socket_t {
public:
    int publish(const std::string &topic,
                message_t &msg,
                send_flag flags = send_flag::none);
    int publish(const std::string &topic,
                std::vector<message_t> &parts,
                send_flag flags = send_flag::none);

protected:
    publisher_socket_t(context_t &ctx, socket_type type);
};
```

### 5.7 `subscriber_socket_t`

역할:

- topic subscribe 계층 facade
- subscription 관리와 topic recv 공개

대상 타입:

- `SUB`
- `XSUB`

고정 인터페이스:

```cpp
class subscriber_socket_t : public base_socket_t {
public:
    int subscribe(const std::string &topic);
    int unsubscribe(const std::string &topic);

    int recv(message_t &msg,
             std::string &topic,
             recv_flag flags = recv_flag::none);
    int recv(std::vector<message_t> &parts,
             std::string &topic,
             recv_flag flags = recv_flag::none);

protected:
    subscriber_socket_t(context_t &ctx, socket_type type);
};
```

## 6. 구체 타입 facade 정의

### 6.1 양방향 message 계열

```cpp
class pair_socket_t   : public message_socket_t { public: explicit pair_socket_t(context_t &ctx); };
class req_socket_t    : public message_socket_t { public: explicit req_socket_t(context_t &ctx); };
class rep_socket_t    : public message_socket_t { public: explicit rep_socket_t(context_t &ctx); };
```

추가 option facade가 필요한 `dealer_socket_t`, `router_socket_t`,
`stream_socket_t`는 6.4에서 최종 형태를 정의한다.

### 6.2 단방향 message 계열

```cpp
class push_socket_t    : public send_socket_t { public: explicit push_socket_t(context_t &ctx); };
class scatter_socket_t : public send_socket_t { public: explicit scatter_socket_t(context_t &ctx); };
class pull_socket_t    : public recv_socket_t { public: explicit pull_socket_t(context_t &ctx); };
class gather_socket_t  : public recv_socket_t { public: explicit gather_socket_t(context_t &ctx); };
```

### 6.3 pub/sub 계열

```cpp
class pub_socket_t : public publisher_socket_t {
public:
    explicit pub_socket_t(context_t &ctx);

    template<typename T>
    int set_option(pub_option option, const T &value);

    template<typename T>
    int get_option(pub_option option, T *value) const;
};

class xpub_socket_t : public publisher_socket_t {
public:
    explicit xpub_socket_t(context_t &ctx);

    template<typename T>
    int set_option(pub_option option, const T &value);

    template<typename T>
    int get_option(pub_option option, T *value) const;
};

class sub_socket_t : public subscriber_socket_t {
public:
    explicit sub_socket_t(context_t &ctx);

    template<typename T>
    int set_option(sub_option option, const T &value);

    template<typename T>
    int get_option(sub_option option, T *value) const;
};

class xsub_socket_t : public subscriber_socket_t {
public:
    explicit xsub_socket_t(context_t &ctx);

    template<typename T>
    int set_option(sub_option option, const T &value);

    template<typename T>
    int get_option(sub_option option, T *value) const;
};
```

### 6.4 타입별 option facade

```cpp
class dealer_socket_t : public message_socket_t {
public:
    explicit dealer_socket_t(context_t &ctx);

    template<typename T>
    int set_option(dealer_option option, const T &value);

    template<typename T>
    int get_option(dealer_option option, T *value) const;
};

class router_socket_t : public message_socket_t {
public:
    explicit router_socket_t(context_t &ctx);

    template<typename T>
    int set_option(router_option option, const T &value);

    template<typename T>
    int get_option(router_option option, T *value) const;
};

class stream_socket_t : public message_socket_t {
public:
    explicit stream_socket_t(context_t &ctx);

    template<typename T>
    int set_option(stream_option option, const T &value);

    template<typename T>
    int get_option(stream_option option, T *value) const;
};
```

정책:

- old STREAM helper는 넣지 않는다.
- STREAM도 `message_socket_t` 기반 `send/recv`만 사용한다.
- 타입별 option facade는 protected base 구현을 재노출하는 얇은 forwarding layer다.

## 7. 허용 인터페이스 매트릭스

| 클래스 | bind/connect | send/recv | publish/subscribe | typed option |
|---|---|---|---|---|
| `pair_socket_t` | O | `send/recv` | X | common |
| `push_socket_t` | O | `send` only | X | common |
| `pull_socket_t` | O | `recv` only | X | common |
| `req_socket_t` | O | `send/recv` | X | common |
| `rep_socket_t` | O | `send/recv` | X | common |
| `dealer_socket_t` | O | `send/recv` | X | common + dealer |
| `router_socket_t` | O | `send/recv` | X | common + router |
| `stream_socket_t` | O | `send/recv` | X | common + stream |
| `pub_socket_t` | O | X | `publish` | common + pub |
| `sub_socket_t` | O | topic `recv` | `subscribe/unsubscribe` | common + sub |
| `xpub_socket_t` | O | X | `publish` | common + pub |
| `xsub_socket_t` | O | topic `recv` | `subscribe/unsubscribe` | common + sub |
| `scatter_socket_t` | O | `send` only | X | common |
| `gather_socket_t` | O | `recv` only | X | common |

의미:

- `push_socket_t`에는 `recv(...)`가 없다.
- `pull_socket_t`에는 `send(...)`가 없다.
- `pub_socket_t`에는 `recv(...)`와 `subscribe(...)`가 없다.
- `sub_socket_t`에는 `send(...)`와 `publish(...)`가 없다.

## 8. 타입별 option 노출 규칙

### 8.1 공통 option

`base_socket_t`에 둔다.

예:

- affinity
- backlog
- linger
- sndhwm / rcvhwm
- sndbuf / rcvbuf
- sndtimeo / rcvtimeo
- reconnect interval
- heartbeat 계열
- tls common 설정
- routing id common getter/setter

### 8.2 타입별 option

해당 facade에만 둔다.

- `router_socket_t`
  - router mandatory
  - handover
  - raw router 관련 옵션
- `dealer_socket_t`
  - dealer reconnect/route 관련 옵션
- `pub_socket_t` / `xpub_socket_t`
  - xpub verbose
  - welcome msg
  - pub 전용 전달 정책
- `sub_socket_t` / `xsub_socket_t`
  - subscription 관련 옵션
- `stream_socket_t`
  - stream notify / raw mode 관련 옵션

정책:

- option enum domain이 이미 core에서 분리되어 있으면 C++도 그대로 분리한다.
- 하나의 generic `set_option(...)`에 모든 enum을 우겨넣지 않는다.
- 사용자에게 "이 옵션이 어느 socket family 소속인지"가 드러나야 한다.

## 9. 생성 정책

직접 `socket_type` enum을 넘겨 generic socket을 만드는 public constructor는 남기지
않는다.

금지:

```cpp
socket_t s(ctx, socket_type::dealer);
```

허용:

```cpp
dealer_socket_t dealer(ctx);
sub_socket_t sub(ctx);
pub_socket_t pub(ctx);
stream_socket_t stream(ctx);
push_socket_t push(ctx);
pull_socket_t pull(ctx);
```

이유:

- 타입 의미가 생성 시점부터 드러난다.
- facade 제한이 실제로 작동한다.
- 문서와 샘플이 단순해진다.

## 10. 서비스 계층과의 관계

아래 클래스는 본 문서의 socket type facade와 별도 계층으로 유지한다.

- `service::registry_t`
- `service::registry_query_client_t`
- `service::discovery_t`
- `service::spot_node_t`
- `service::spot_t`

정책:

- `spot_t`는 raw `pub_socket_t`/`sub_socket_t`의 단순 별칭이 아니다.
- discovery attach는 raw socket과 service layer 모두 지원하되 진입점은 대상
  핸들 쪽에 둔다.

## 11. migration 기준

현재 `socket_t` 중심 코드를 아래처럼 옮긴다.

- `socket_t(ctx, socket_type::pair)` -> `pair_socket_t(ctx)`
- `socket_t(ctx, socket_type::dealer)` -> `dealer_socket_t(ctx)`
- `socket_t(ctx, socket_type::router)` -> `router_socket_t(ctx)`
- `socket_t(ctx, socket_type::stream)` -> `stream_socket_t(ctx)`
- `socket_t(ctx, socket_type::pub)` -> `pub_socket_t(ctx)`
- `socket_t(ctx, socket_type::sub)` -> `sub_socket_t(ctx)`
- `socket_t(ctx, socket_type::push)` -> `push_socket_t(ctx)`
- `socket_t(ctx, socket_type::pull)` -> `pull_socket_t(ctx)`
- `socket_t(ctx, socket_type::scatter)` -> `scatter_socket_t(ctx)`
- `socket_t(ctx, socket_type::gather)` -> `gather_socket_t(ctx)`

구형 generic 작성 패턴은 `compat.hpp`에서도 복구하지 않는다.

## 12. 구현 순서

### Slice 1. ownership/base 분리

- `socket_handle_t` 추출
- 기존 `socket_t` 공통 lifecycle/option/monitor 코드를 `base_socket_t`로 이동
- `send_socket_t`, `recv_socket_t`, `message_socket_t` 골격 추가
- 기존 테스트 빌드 유지

완료 조건:

- 기존 contract test가 컴파일/통과
- `base_socket_t`가 직접 data-plane public API를 노출하지 않음
- 단방향 socket에 반대 방향 API가 public으로 노출되지 않음

### Slice 2. data-plane facade 분리

- `publisher_socket_t`
- `subscriber_socket_t`
- topic recv/publish helper 정리

완료 조건:

- `send/recv`와 `publish/subscribe`가 public surface에서 분리됨

### Slice 3. concrete type facade 추가

- `pair_socket_t`, `dealer_socket_t`, `router_socket_t`, `pub_socket_t`,
  `sub_socket_t`, `stream_socket_t` 우선 추가
- 이후 `push/pull/scatter/gather/xpub/xsub` 추가

완료 조건:

- 샘플과 contract test가 concrete type facade를 사용

### Slice 4. typed option domain 정리

- 타입별 facade에 domain-specific option API 추가
- generic socket-type misuse 제거

완료 조건:

- 타입별 샘플에서 자기 타입 option만 사용

### Slice 5. generic constructor 제거

- `socket_t(context_t&, socket_type)` public 진입 제거
- `socket.hpp` compat 축소
- migration 문서 반영

완료 조건:

- 사용자-facing 샘플/테스트가 concrete type facade만 사용

## 13. 완료 기준

아래가 모두 만족되면 본 설계를 구현 완료로 본다.

- `socket_type` enum 기반 generic public 생성자가 사라짐
- 타입별 facade가 실제 public surface를 제한함
- `send/recv` 계열과 `publish/subscribe` 계열이 분리됨
- 단방향 socket의 잘못된 API가 compile-time에 차단됨
- router/dealer/pub/sub/stream option이 각 facade에 노출됨
- 기존 C++ 샘플이 concrete type facade 기준으로 재작성됨
- contract test와 sample-smoke가 모두 통과함

## 14. 보류 사항

이번 설계에서는 아래를 넣지 않는다.

- `std::function` 기반 callback facade
- template serializer/deserializer
- CRTP 기반 socket trait metaprogramming
- compile-time static_assert matrix를 위한 복잡한 traits 체계

이유:

- public 의미를 단순하게 만드는 데 직접 필요하지 않다.
- 구현 복잡도 대비 유지 이점이 작다.

## 15. 최종 결론

이번 C++ socket 계층은 아래 방향으로 고정한다.

- 구현은 `base_socket_t`에 집중
- public surface는 타입별 facade로 분리
- `send/recv`와 `publish/subscribe`를 의미 계층 기준으로 구분
- 단방향/양방향 transport도 facade 계층에서 분리
- option도 socket family 기준으로 분리

즉, "generic socket 하나에 모든 기능을 넣는 구조"와 "타입별 구현을 전부 복제하는
구조" 사이에서, 공통 구현은 깊게 유지하고 public surface만 의미 중심으로
나누는 형태를 채택한다.
