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
- 문서에 등장하는 concrete socket type은 실제 `core/include/zlink.h`에 존재하는
  native socket type만 사용한다.

## 3. native 기준 확정 범위

이 문서가 전제로 삼는 raw socket type은 최신 `core`가 실제로 제공하는 아래 8종뿐이다.

- `PAIR`
- `PUB`
- `SUB`
- `DEALER`
- `ROUTER`
- `XPUB`
- `XSUB`
- `STREAM`

이번 설계에서 제외하는 타입:

- `PUSH`
- `PULL`
- `SCATTER`
- `GATHER`
- `REQ`
- `REP`

제외 이유:

- 최신 [`zlink.h`](/home/hep7/project/kairos/zlink/core/include/zlink.h) 에 native socket
  type이 없다.
- C++에서만 가상 facade로 부활시키면 shallow wrapper와 change amplification만 만든다.

## 4. 최종 계층 구조

```text
socket_handle_t
  ^
  |
base_socket_t
  ^
  +-- message_socket_t
  |     +-- pair_socket_t
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
- `message_socket_t`는 raw message transport facade다.
- `publisher_socket_t`, `subscriber_socket_t`는 topic 의미를 분리하는 facade다.
- concrete type은 public surface 제한과 타입별 option 노출만 담당한다.

## 5. 네임스페이스와 헤더 배치

최종 헤더 배치는 아래로 고정한다.

- `include/zlink/socket_handle.hpp`
- `include/zlink/base_socket.hpp`
- `include/zlink/message_socket.hpp`
- `include/zlink/publisher_socket.hpp`
- `include/zlink/subscriber_socket.hpp`
- `include/zlink/socket_types.hpp`

`socket_types.hpp`에는 구체 타입 facade를 모은다.

- `pair_socket_t`
- `dealer_socket_t`
- `router_socket_t`
- `stream_socket_t`
- `pub_socket_t`
- `sub_socket_t`
- `xpub_socket_t`
- `xsub_socket_t`

`include/zlink.hpp`는 위 헤더를 최종 public umbrella에 포함한다.

추가 정책:

- 기존 [`socket.hpp`](/home/hep7/project/kairos/zlink/bindings/cpp/include/zlink/socket.hpp)
  는 즉시 삭제하지 않는다.
- `socket.hpp`는 새 socket 계층 헤더를 재노출하는 umbrella/compat header로 축소한다.
- 새 샘플과 새 contract test는 `socket_t` generic 생성자를 사용하지 않는다.

## 6. 클래스별 책임

### 6.1 `socket_handle_t`

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

### 6.2 `base_socket_t`

역할:

- 공통 lifecycle API
- 공통 endpoint API
- 공통 option API
- monitor/service monitor attach
- callback registration 공통 처리
- discovery attach 공통 처리
- routing id / TLS 공통 helper
- 타입별 option domain의 구현 보관

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

protected:
    int set_router_option(router_option option, const void *value, size_t size);
    int get_router_option(router_option option, void *value, size_t *size) const;

    int set_router_option(router_option_key_t<std::string> key,
                          const std::string &value);
    int get_router_option(router_option_key_t<std::string> key,
                          std::string &value) const;

    template<typename T>
    int set_router_option(router_option_key_t<T> key, const T &value);

    template<typename T>
    int get_router_option(router_option_key_t<T> key, T *value) const;

    int set_dealer_option(dealer_option option, const void *value, size_t size);

    template<typename T>
    int set_dealer_option(dealer_option_key_t<T> key, const T &value);

    int set_pub_option(pub_option option, const void *value, size_t size);
    int get_pub_option(pub_option option, void *value, size_t *size) const;

    int set_pub_option(pub_option_key_t<std::string> key,
                       const std::string &value);
    int get_pub_option(pub_option_key_t<std::string> key,
                       std::string &value) const;

    template<typename T>
    int set_pub_option(pub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_pub_option(pub_option_key_t<T> key, T *value) const;

    int set_sub_option(sub_option option, const void *value, size_t size);
    int get_sub_option(sub_option option, void *value, size_t *size) const;

    template<typename T>
    int set_sub_option(sub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_sub_option(sub_option_key_t<T> key, T *value) const;

    int set_stream_option(stream_option option, const void *value, size_t size);
    int get_stream_option(stream_option option,
                          void *value,
                          size_t *size) const;

    template<typename T>
    int set_stream_option(stream_option_key_t<T> key, const T &value);

    template<typename T>
    int get_stream_option(stream_option_key_t<T> key, T *value) const;

    base_socket_t(context_t &ctx, socket_type type);
    explicit base_socket_t(void *socket, bool own = true) noexcept;
};
```

제약:

- `base_socket_t`에는 `send`, `recv`, `publish`, `subscribe`를 직접 public으로
  두지 않는다.
- data-plane 동작은 하위 facade가 의미에 맞게 노출한다.
- 타입별 option domain API는 `base_socket_t`에 구현하되 protected로 둔다.
- 공통 option과 타입별 option 모두 typed key를 기본 public 경로로 사용한다.
- dealer option은 native get API가 없으므로 set만 제공한다.

### 6.3 `message_socket_t`

역할:

- raw message transport facade
- `send/recv` overload 공개

대상 타입:

- `PAIR`
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

- `send(const zlink_routing_id_t&, ...)` 와 `recv(zlink_routing_id_t&, ...)` 는
  `message_socket_t`에 공통으로 두되, 실제 사용 의미가 있는 타입은 주로
  `dealer/router/stream`이다.
- `pair_socket_t`에서 routing id 계열을 남길지 숨길지는 구현 단계에서 한 번 더
  보지 않는다. 이번 설계에서는 common raw transport surface로 유지한다.

### 6.4 `publisher_socket_t`

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

정책:

- `publisher_socket_t`는 data publish만 공통으로 가진다.
- `XPUB`의 subscription event 수신은 `xpub_socket_t` concrete facade에서 별도로
  노출한다.

### 6.5 `subscriber_socket_t`

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
    int set_subscription(const std::string &topic);
    int unset_subscription(const std::string &topic);
    int subscription_at(size_t index,
                        std::string &topic,
                        bool *is_pattern = NULL) const;

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

정책:

- subscription 관리 함수 이름은 native `zlink_set_subscription()`,
  `zlink_unset_subscription()`, `zlink_subscription_at()`와 맞춘다.
- `subscribe(...)`라는 이름은 recv 동작과 혼동되므로 socket facade에서 쓰지 않는다.

## 7. 구체 타입 facade 정의

### 7.1 message 계열

```cpp
class pair_socket_t : public message_socket_t {
public:
    explicit pair_socket_t(context_t &ctx);
};

class dealer_socket_t : public message_socket_t {
public:
    explicit dealer_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(dealer_option_key_t<T> key, const T &value);
};

class router_socket_t : public message_socket_t {
public:
    explicit router_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(router_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(router_option_key_t<T> key, T *value) const;
};

class stream_socket_t : public message_socket_t {
public:
    explicit stream_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(stream_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(stream_option_key_t<T> key, T *value) const;
};
```

정책:

- `dealer_socket_t`는 `set_option(dealer_option_key_t<T>, ...)`만 제공한다.
- `router_socket_t`, `stream_socket_t`는 native get/set이 모두 있으므로 둘 다 제공한다.
- old STREAM helper는 넣지 않는다.

### 7.2 pub/sub 계열

```cpp
class pub_socket_t : public publisher_socket_t {
public:
    explicit pub_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(pub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(pub_option_key_t<T> key, T *value) const;
};

class xpub_socket_t : public publisher_socket_t {
public:
    explicit xpub_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(pub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(pub_option_key_t<T> key, T *value) const;

    int subscription_event(zlink_routing_id_t &source_rid,
                           bool &subscribed,
                           std::string &topic,
                           recv_flag flags = recv_flag::none);
    int subscription_event(bool &subscribed,
                           std::string &topic,
                           recv_flag flags = recv_flag::none);
};

class sub_socket_t : public subscriber_socket_t {
public:
    explicit sub_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(sub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(sub_option_key_t<T> key, T *value) const;
};

class xsub_socket_t : public subscriber_socket_t {
public:
    explicit xsub_socket_t(context_t &ctx);

    template<typename T>
    template<typename T>
    int set_option(sub_option_key_t<T> key, const T &value);

    template<typename T>
    int get_option(sub_option_key_t<T> key, T *value) const;
};
```

typed option 예시:

```cpp
router.set_option(zlink::router_options::mandatory, 1);
router.set_option(zlink::router_options::connect_routing_id,
                  std::string("router-alpha"));

xpub.set_option(zlink::pub_options::manual, 1);
xpub.set_option(zlink::pub_options::welcome_msg,
                std::string("hello"));

stream.set_option(zlink::stream_options::notify, 1);
```

## 8. 허용 인터페이스 매트릭스

| 클래스 | bind/connect | send/recv | publish/subscribe | typed option |
|---|---|---|---|---|
| `pair_socket_t` | O | `send/recv` | X | common |
| `dealer_socket_t` | O | `send/recv` | X | common + dealer(set only) |
| `router_socket_t` | O | `send/recv` | X | common + router |
| `stream_socket_t` | O | `send/recv` | X | common + stream |
| `pub_socket_t` | O | X | `publish` | common + pub |
| `sub_socket_t` | O | topic `recv` | `set/unset_subscription` | common + sub |
| `xpub_socket_t` | O | `subscription_event` | `publish` | common + pub |
| `xsub_socket_t` | O | topic `recv` | `set/unset_subscription` | common + sub |

의미:

- `pub_socket_t`에는 `recv(...)`와 `subscribe(...)`가 없다.
- `sub_socket_t`에는 `send(...)`와 `publish(...)`가 없다.
- `xpub_socket_t`는 data `publish(...)` 외에 subscription event recv를 가진다.
- `dealer_socket_t`에는 dealer-specific `get_option(...)`이 없다.

## 9. 타입별 option 노출 규칙

### 9.1 공통 option

`base_socket_t` public API에 둔다.

예:

- affinity
- backlog
- linger
- sndhwm / rcvhwm
- sndbuf / rcvbuf
- sndtimeo / rcvtimeo
- reconnect interval
- heartbeat 계열
- routing id helper
- TLS helper

### 9.2 타입별 option

해당 facade에만 둔다.

- `router_socket_t`
  - router mandatory
  - handover
  - raw router 관련 옵션
- `dealer_socket_t`
  - dealer 전용 set-only 옵션
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
- 호출자가 옵션 값 타입을 기억하지 않도록 typed key 상수를 함께 제공한다.
- 사용자에게 "이 옵션이 어느 socket family 소속인지"가 드러나야 한다.

## 10. 생성 정책

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
```

이유:

- 타입 의미가 생성 시점부터 드러난다.
- facade 제한이 실제로 작동한다.
- 문서와 샘플이 단순해진다.

## 11. 서비스 계층과의 관계

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

## 12. migration 기준

현재 `socket_t` 중심 코드를 아래처럼 옮긴다.

- `socket_t(ctx, socket_type::pair)` -> `pair_socket_t(ctx)`
- `socket_t(ctx, socket_type::dealer)` -> `dealer_socket_t(ctx)`
- `socket_t(ctx, socket_type::router)` -> `router_socket_t(ctx)`
- `socket_t(ctx, socket_type::stream)` -> `stream_socket_t(ctx)`
- `socket_t(ctx, socket_type::pub)` -> `pub_socket_t(ctx)`
- `socket_t(ctx, socket_type::sub)` -> `sub_socket_t(ctx)`
- `socket_t(ctx, socket_type::xpub)` -> `xpub_socket_t(ctx)`
- `socket_t(ctx, socket_type::xsub)` -> `xsub_socket_t(ctx)`

구형 generic 작성 패턴은 `compat.hpp`에서도 복구하지 않는다.

## 13. 구현 순서

### Slice 1. ownership/base 분리

- `socket_handle_t` 추출
- 기존 `socket_t` 공통 lifecycle/option/monitor 코드를 `base_socket_t`로 이동
- `message_socket_t`, `publisher_socket_t`, `subscriber_socket_t` 골격 추가
- 기존 테스트 빌드 유지

완료 조건:

- 기존 contract test가 컴파일/통과
- `base_socket_t`가 직접 data-plane public API를 노출하지 않음

### Slice 2. concrete facade 추가

- `pair_socket_t`, `dealer_socket_t`, `router_socket_t`, `pub_socket_t`,
  `sub_socket_t`, `stream_socket_t` 우선 추가
- 이후 `xpub_socket_t`, `xsub_socket_t` 추가

완료 조건:

- 샘플과 contract test가 concrete type facade를 사용

### Slice 3. typed option domain 정리

- 타입별 facade에 domain-specific option API 추가
- dealer set-only 계약 반영
- generic socket-type misuse 제거

완료 조건:

- 타입별 샘플에서 자기 타입 option만 사용

### Slice 4. generic constructor 제거

- `socket_t(context_t&, socket_type)` public 진입 제거
- `socket.hpp` compat 축소
- migration 문서 반영

완료 조건:

- 사용자-facing 샘플/테스트가 concrete type facade만 사용

### Slice 5. POSD 기반 최종 리팩토링

- John Ousterhout POSD 기준으로 전체 socket facade 구조를 다시 리뷰
- `base_socket_t`, `socket.hpp`, concrete facade 사이에 남은 shallow wrapper,
  중복 option 위임, 설명 비용이 큰 compat surface를 정리
- 리팩토링은 한 번으로 끝내지 않고, 더 이상 의미 있는 리팩토링 대상이
  없다고 판단될 때까지 반복

반복 종료 기준:

- public surface 설명이 더 짧아지지 않는다.
- 내부 중복 제거 여지가 더 이상 없다.
- 변경 대비 복잡도 감소 효과가 충분하지 않다.
- 이후 변경은 구조 개선보다 churn 성격이 강하다.

완료 조건:

- 최종 구조를 POSD 기준으로 설명할 때 change amplification, hidden coupling,
  shallow wrapper 문제가 남아 있지 않다.
- 빌드/contract/sample-smoke 검증이 모두 녹색이다.

## 14. 완료 기준

아래가 모두 만족되면 본 설계를 구현 완료로 본다.

- `socket_type` enum 기반 generic public 생성자가 사라짐
- concrete facade 목록이 최신 native socket type과 정확히 일치함
- 타입별 facade가 실제 public surface를 제한함
- `send/recv` 계열과 `publish/subscribe` 계열이 분리됨
- dealer/router/pub/sub/stream option domain이 native 계약과 동일하게 노출됨
- 공통 routing id/TLS helper가 실제 public API로 제공됨
- 기존 C++ 샘플이 concrete type facade 기준으로 재작성됨
- POSD 기준 최종 리팩토링이 끝나고 더 이상 구조 개선 대상이 남아 있지 않음
- contract test와 sample-smoke가 모두 통과함

## 15. 보류 사항

이번 설계에서는 아래를 넣지 않는다.

- `std::function` 기반 callback facade
- template serializer/deserializer
- CRTP 기반 socket trait metaprogramming
- native에 없는 socket type facade

이유:

- public 의미를 단순하게 만드는 데 직접 필요하지 않다.
- 구현 복잡도 대비 유지 이점이 작다.

## 16. 최종 결론

이번 C++ socket 계층은 아래 방향으로 고정한다.

- 구현은 `base_socket_t`에 집중
- public surface는 concrete socket facade로 분리
- `send/recv`와 `publish/subscribe`를 의미 계층 기준으로 구분
- option도 socket family 기준으로 분리
- native에 없는 socket type은 C++에서 만들지 않음

즉, "generic socket 하나에 모든 기능을 넣는 구조"와 "타입별 구현을 전부 복제하는
구조" 사이에서, 공통 구현은 깊게 유지하고 public surface만 의미 중심으로
나누는 형태를 채택한다.
