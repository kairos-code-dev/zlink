# C++ common runtime exact interface

[C++ exact interface 목차](README.ko.md)

<!-- framework-adapter-nav:start -->
[스펙 목차](README.ko.md) | [이전: C++ 시스템 구조](../01-system-structure.ko.md) | [다음: C++ HTTP Hosting](../60-http-hosting.ko.md)
<!-- framework-adapter-nav:end -->

[스펙 목차](../../../../../README.ko.md)

> 이 문서는 ZLink Framework 11.0.0의 C++ 정식 public interface 계약이다.
> 이 문서는 `framework/doc/framework/common/spec` 아래 공통 Framework 정책을 상위 기준으로 따르고,
> C++ binding의 public 라이브러리 표면을 기반으로 framework 계층을 설계한다.

## 1. 계약 기준

`C++` framework는 C++ binding을 대체하지 않는다. framework는 C++ binding 위에
올라가며, binding이 제공하는 typed public API를 내부 runtime substrate로 사용한다.

기능과 사용성 개념은 framework 공통 스펙을 기준으로 맞춘다. 즉 app/host, DI scope,
handler registry, channel messaging, `STREAM`, `SPOT`, ActorGateway session relay,
monitoring, graceful shutdown은 같은 모델을 제공하고, C++ public API는 C++20 coroutine,
callback, RAII ownership에 맞게 표현만 바꾼다.

binding 기준은 아래 문서를 따른다.

- [C++ Binding Specification](../../../../../../../../../bindings/doc/spec/cpp/README.md)
- [C++ Codec Extension Specification](../../../../../../../../../bindings/doc/spec/cpp/codec.md)

framework public API는 `zlink::framework` namespace 아래에 둔다. 설치되는 public header에는
정식 contract와 명시적인 extension point만 포함한다. 일반 사용자는 raw socket이나 poller를 직접
다루지 않고 application을 구성할 수 있어야 한다.

## 2. Binding public dependency 경계

Framework package는 C++ binding의 public API만 의존한다. Application은 framework 기능을 사용할 때
binding의 native handle, raw callback userdata, raw option key, socket과 poller를 직접 다루지 않는다.
공개 handler와 client에는 ChannelName, topic, typed payload, timeout과 lifecycle처럼 framework
계약에 정의된 값만 나타난다.

사용자가 binding 값을 직접 넘길 수 있는 곳은 `message_t`처럼 정식 signature가 명시한 payload
경계로 제한한다. 그 밖의 binding 타입은 framework public signature에 나타나지 않는다.

## 3. Header 와 Namespace

권장 public header layout은 아래와 같다. `contracts/*` 아래 header가 `.NET`
`Contracts/*`에 대응하는 실제 public contract owner이고, `zlink/framework.hpp`는
사용자가 전체 framework 표면을 한 번에 include할 수 있는 facade다. 한 줄짜리
`zlink/framework/*.hpp` compatibility wrapper는 유지하지 않는다.

```text
zlink/framework.hpp
zlink/framework/version.hpp
zlink/framework/contracts/actors/*.hpp
zlink/framework/contracts/channels/*.hpp
zlink/framework/contracts/codecs/*.hpp
zlink/framework/contracts/configuration/*.hpp
zlink/framework/contracts/dispatch/*.hpp
zlink/framework/contracts/errors/*.hpp
zlink/framework/contracts/eventing/*.hpp
zlink/framework/contracts/handlers/*.hpp
zlink/framework/contracts/http/*.hpp
zlink/framework/contracts/locations/*.hpp
zlink/framework/contracts/messaging/*.hpp
zlink/framework/contracts/spots/*.hpp
zlink/framework/contracts/streams/*.hpp
zlink/framework/contracts/timers/*.hpp
zlink/framework/contracts/workers/*.hpp
```

`zlink/framework/runtime.hpp` 같은 public header는 제공하지 않는다. public API에는 `app_t`,
`request_client_t`, `spot_context_t`처럼 사용자가 이해하는 계약 이름만 노출한다.

`bindings/cpp`보다 framework 쪽의 분리를 더 강하게 잡는다. binding은 zlink core의
native 개념을 C++로 안전하게 감싸는 계층이지만, framework는 application contract를
제공하는 계층이다. 그래서 framework contract header가 binding public 타입을 내부
substrate로 참조할 수는 있어도, native socket [owner](../../../../01-glossary.ko.md#owner), CAPI dispatch callback, raw recv
순서, frame codec 구현을 public contract로 노출하면 안 된다.

public header에 template 구현이 필요한 경우에는 `contracts/detail/*`만 사용한다.
이 detail 영역은 type trait, concept check, facade forwarding을 위한 곳이며,
runtime 구현을 숨겨 넣는 장소가 아니다.

이 구조는 `.NET`의 public interface를 C++ pure virtual class로 모두 옮긴다는 뜻이
아니다. C++ public API는 concrete facade와 value type을 적극적으로 사용할 수 있다.
다만 facade의 멤버, 생성자, method signature가 runtime 구현 타입을 노출하지 않아야 한다.
runtime 객체를 가리켜야 하는 public facade는 PIMPL, type-erased state, shared internal
state 같은 방식으로 구현을 숨긴다. 사용자 확장점만 abstract interface 또는 concept
contract로 둔다.

설치되는 header는 contract와 facade만 포함한다. 구현 전용 header는 install 결과와 package의 public
include 경로에 포함하지 않는다.

public type을 만들 때는 아래 질문에 모두 답해야 한다.

| 질문 | public contract에 둘 수 있는 경우 | runtime에 숨겨야 하는 경우 |
|------|----------------------------------|-----------------------------|
| 사용자가 직접 구현하는가? | handler, filter, serializer, hosted service처럼 구현 대상이면 둔다. | framework가 내부에서만 구현하면 숨긴다. |
| 사용자가 값을 조합하는가? | option, builder, typed result처럼 조합 대상이면 둔다. | queue node, dispatch token, recv state처럼 조합하지 않으면 숨긴다. |
| 공통 기능을 사용자에게 제공하는가? | 같은 기능 축의 public 계약이면 C++ contract로 둔다. | runtime 실행에만 필요한 타입이면 숨긴다. |
| native 실행 순서를 드러내는가? | 드러내지 않으면 facade로 둘 수 있다. | poll/recv/drain 순서가 보이면 숨긴다. |

### 3.1 공개 계약 경계

C++ 공개 header는 사용자가 구성하거나 호출하는 타입과 결과만 정의한다. socket owner, queue,
pending operation 저장소, dispatch 순서와 native transport adapter는 공개 signature에 노출하지
않는다. 공개 facade가 상태를 유지해야 할 때도 사용자는 그 상태의 자료구조나 처리 순서를 알 필요가
없어야 한다.

공개 `route_client_t`와 `route_send_call_t`는 node와 global Spot ID를 대상으로 하는 typed 호출을
제공한다. [User Spot](../../../../01-glossary.ko.md#entry-user-instance-spot)과 Instance Spot은 같은 ID-only 호출 표면을 사용하며, 별도 handle·resolver·논리 주소
타입을 제공하지 않는다. request 계열은 `channel_request_call_t`을 반환한다. 사용자는 target MeshNode,
location owner token, generation이나 retry 절차를 넘기지 않으며 routing envelope, location claim과
serializer 선택은 framework가 처리한다.

일반 request는 `request_to_node(...).timeout(...).async<TReply>()`로 typed reply를 받는다.
`.metadata(key, value)`로 설정한 값은 application metadata 계약에 따라 snapshot되며, transport
세부와 correlation 상태는 공개 API에 드러나지 않는다.

native result와 error envelope는 다음 공개 오류 의미로 변환한다.

| native/error code | C++ error kind | retriable |
|-------------------|----------------|-----------|
| `timed_out`, `timeout` | 경계 timeout — public enum 값이 아니라 `framework_exception_t`의 `code() == std::errc::timed_out` 값(§7.4) | no |
| `not_connected`, `route_not_connected` | `route_not_connected` | yes |
| `not_found`, `request_target_not_found` | `request_target_not_found` | no |
| `rejected`, `request_rejected` | `request_rejected` | no |
| `busy`, `conflict` | `request_rejected` | yes |
| `protocol_error`, `request_protocol_error` | `request_protocol_error` | no |
| `handler_not_found` | `handler_not_found` | no |

이 표는 request completion과 error envelope reply에 같은 의미로 적용한다.

DTO message name은 `static constexpr const char *packet_name`을 우선 사용한다. framework
handler 등록과 Stream Connector의 send, request와 on 기본 이름은 이 값을 읽는다. 이름이 없는
타입은 C++ type name을 사용할 수 있지만, 공개 sample과 정식 DTO는 명시적인 packet name을
가져야 한다.
### 3.2 C++ 공개 header 제약

C++는 설치된 header가 곧 공개 표면이므로 다음 규칙을 지킨다.

- template header에는 type check와 공개 facade forwarding만 둔다.
- public class의 state는 공개 계약 타입만 사용하며 native socket, queue와 pending operation 타입을
  노출하지 않는다.
- JSON, MessagePack, Protobuf와 같은 선택 dependency 타입은 해당 codec extension의 공개 계약에만
  나타날 수 있다.
- contract test는 설치된 public header만 include한다.
- public inline 함수는 공개 validation과 forwarding을 넘어서 transport state를 조작하지 않는다.

모든 framework 타입은 `zlink::framework` namespace 아래에 둔다. 각 타입의
declaration은 [exact interface 목차](README.ko.md)에서 지정한 단 하나의 범주 문서가
소유한다.

## 4. Common result, coroutine과 message

```cpp
namespace zlink::framework {

template <typename T>
class result_t {
public:
    static result_t success(T value);
    static result_t failure(
      framework_error_kind_t kind,
      std::string message,
      bool retriable = false);
    bool has_value() const noexcept;
    explicit operator bool() const noexcept;
    const T &value() const;
    T &value();
    const framework_exception_t *error() const noexcept;
    framework_error_kind_t error_kind() const;
};

template <>
class result_t<void> {
public:
    static result_t success();
    static result_t failure(
      framework_error_kind_t kind,
      std::string message,
      bool retriable = false);
    bool has_value() const noexcept;
    explicit operator bool() const noexcept;
    void value() const;
    const framework_exception_t *error() const noexcept;
    framework_error_kind_t error_kind() const;
};

template <typename T>
class task_t {
public:
    struct promise_type {
        task_t get_return_object();
        std::suspend_never initial_suspend() noexcept;
        std::suspend_never final_suspend() noexcept;
        void unhandled_exception();
        void return_value(result_t<T> result);

        template <typename U>
        void return_value(U &&value);
    };

    explicit task_t(result_t<T> result);
    task_t(task_t &&) noexcept = default;
    task_t &operator=(task_t &&) noexcept = default;
    task_t(const task_t &) = delete;
    task_t &operator=(const task_t &) = delete;
    ~task_t() = default;
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> continuation);
    T await_resume();
    const result_t<T> &result() const;
};

template <>
class task_t<void> {
public:
    struct promise_type {
        task_t get_return_object();
        std::suspend_never initial_suspend() noexcept;
        std::suspend_never final_suspend() noexcept;
        void unhandled_exception();
        void return_void() noexcept;
    };

    explicit task_t(result_t<void> result);
    task_t(task_t &&) noexcept = default;
    task_t &operator=(task_t &&) noexcept = default;
    task_t(const task_t &) = delete;
    task_t &operator=(const task_t &) = delete;
    ~task_t() = default;
    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> continuation);
    void await_resume();
    const result_t<void> &result() const;
};

class message_t {
public:
    message_t() = default;

    template <typename TValue>
    static message_t from(TValue value);

    template <typename TValue>
    TValue decode() const;

    bool encoded() const noexcept;
    bool empty() const noexcept;
};

} // namespace zlink::framework
```

## 5. Serialization

Framework는 typed JSON serializer를 기본 경로로 사용한다. Handler와 messaging API는 payload type을
받으며 application이 registry, type-erased pointer, encoder callback이나 raw dispatch table을 다루지 않는다.
JSON으로 표현할 수 없는 payload는 codec extension package를 `options.codecs().use(...)`로 선택한다. Extension
package의 registry 연결과 payload 변환은 runtime 내부 계약이며 application public header에 노출하지 않는다.
Framework, connector와 HTTP client가 codec을 바꿔도 handler와 client의 typed API는 바뀌지 않는다.

```cmake
target_link_libraries(app PRIVATE zlink::cpp)

# Protobuf가 필요할 때만 추가한다.
target_link_libraries(app PRIVATE zlink::framework_codec_protobuf)
```

## 6. C++ 고유 계약

### 6.1 Backpressure

SPOT과 STREAM의 backpressure는 public **call object, timeout, result error kind**로만 관찰한다.

- **application handler가 pending queue를 직접 resume하거나 poller readiness를 다루는 API를 두지
  않는다.**
- **기본 정책은 무한 queue가 아니다.** queue 상한·submit timeout·overflow 정책은 framework runtime
  설정으로 닫고, **한도 초과는 실패 result로 반환한다**(`request_rejected` 등).

### 6.2 Handler filter

**filter는 `handler_invocation_context_t`로 descriptor·dispatch context·immutable message payload를
읽는다.** **payload를 바꾸려면 `next()` 결과 대신 새 `message_t`를 반환한다.**

filter의 등록 순서·`next` 의미·scope는 [framework API §8.1](../../../../05-framework-api.ko.md)이
소유한다.

### 6.3 Public surface 경계

- **public surface는 native socket, poller, callback userdata를 직접 노출하지 않는다.**
- **handler public contract는 `contracts/handlers/*`가 소유한다.** handler [descriptor](../../../../01-glossary.ko.md#descriptor) map, DI
  resolve, serializer 호출 순서와 dispatch lookup은 public signature에 노출하지 않는다.
- **handler template 코드는 지원하는 handler signature인지 검사하고 type-erased 호출로 연결하는 작업으로
  제한한다.** pending queue,
  recv loop, monitoring event 생성 구현을 `contracts/detail/*`에 넣지 않는다.


### 6.4 Timer 실행

**C++ framework는 binding의 public generic timer를 사용하고 만료 event를 owner [Spot](../../../../01-glossary.ko.md#spot) mailbox에 제출한다.**
Timer callback, packet과 Actor turn은 같은 owner의 serial execution queue에서 순서를 정한다. Native timer
handle, poller와 receive 순서는 public interface에 나타나지 않는다.

**CPU-bound이거나 blocking 가능성이 있는 handler는 Framework runtime의 offload 실행으로 넘긴다**
(§7.3 worker).

### 6.5 Actor gateway 결정

| 항목 | 결정 |
|------|------|
| **`actor_ref_t` public 형태** | node routing id, actor id, **generation**을 담는 C++ 값 타입. **native 내부 ref를 그대로 노출하지 않는다** |
| **session 생성** | session 구현체는 **DI에서 resolve한다.** handler registry callback은 낮은 수준 확장 표면으로만 둔다 |
| **remote ActorGateway locator codec** | wire metadata는 **runtime 내부 frame으로 숨긴다.** application에는 `actor_ref_t`와 session actor 표면만 보인다 |
| **actor factory 중복 정책** | 같은 actor id 중복은 **`actor_already_exists`**, actor id/type 불일치는 **`actor_type_mismatch`** 로 보고한다 |

**`actor_ref_t`의 `node_rid`·`actor_id`·`generation`은 bind·relay·push round-trip에서 보존된다.**
**local actor relay와 remote actor relay는 같은 public 표면을 쓴다.**

## 7. Public 타입 카탈로그

**이 절은 위 절들이 다루지 않은 public 타입을 채운다.** 여기 없는 `*_state_t`·`*_snapshot_t`는
**runtime 내부 상태**이며 공개 계약이 아니다.

### 7.1 Dispatch 오류 계약

Dispatch 실패는 별도 event type을 만들지 않고 [Monitoring §2](08-monitoring.ko.md#2-메시지-흐름-관측)의
`message_flow_event_t`로
표현한다. `surface`, `message_kind`, `reason`, `action`의 닫힌 값과 조건부 field 규칙은
[메시지 흐름 추적 §3~§4](../../../../52-message-flow-tracing.ko.md)이 소유한다.

### 7.2 Dispatch 실행 정책

`handler_execution_t`는 handler 실행 방식을 구분한다. Dispatch 진단, message-flow과 error
event의 exact declaration은 [Monitoring interface](08-monitoring.ko.md)가 소유한다.

### 7.3 Worker

```cpp
template <typename TResult> class worker_call_t
{
public:
    using executor_t = std::function<task_t<TResult>(
      std::stop_token)>;

    worker_call_t() = default;
    explicit worker_call_t(executor_t executor);
    worker_call_t &timeout (std::chrono::milliseconds value);
    void submit ();
    task_t<TResult> async ();
    task_t<TResult> yield ();
};

class worker_options_t {
public:
    std::size_t min_threads() const noexcept;
    worker_options_t &min_threads(std::size_t value);
    std::size_t max_threads() const noexcept;
    worker_options_t &max_threads(std::size_t value);
    std::chrono::milliseconds idle_timeout() const noexcept;
    worker_options_t &idle_timeout(std::chrono::milliseconds value);
    std::size_t max_queue_length() const noexcept;
    worker_options_t &max_queue_length(std::size_t value);
};
```

**worker는 spot·session 실행 문맥 밖에서 실행하는 작업이다.** 완료를 원래 실행 문맥에서 재개하는
규칙은 [비동기 실행 정책](../../../../04-async-execution-policy.ko.md)이 소유한다. Worker function에는 timeout,
host 종료와 caller cancellation을 합친 `std::stop_token`을 전달한다. `submit()`은 결과를 기다리지
않는 terminal이고 `async()`는 현재 turn을 유지하며 결과를 기다린다. `yield()`는 `SpotWide` User Spot
또는 Instance Spot의 shared turn에서만 그 turn을 반환하고 결과를 기다린다. 다른 실행 문맥에서는
worker를 제출하거나 turn을 반환하지 않고 `invalid_configuration`으로 완료한다.
`worker_options_t`의 최소·최대 thread 수, idle timeout과 queue 상한은 host 시작 전에만 설정한다.

### 7.4 오류 경계

동기 validation과 명시적인 결과 객체를 반환하는 API는 `result_t<T>`로 실패를 반환한다. 비동기 call의
`async()`는 실패하면 같은 오류 정보를 가진 `framework_exception_t`를 throw한다. 오류 code는
`framework_exception_t::code()`의 `std::error_code`로 노출한다.


### 7.5 실행 문맥

같은 Spot의 dispatch가 직렬화되는 근거는
[stage-wrapper §3](../../../../25-stage-wrapper-on-spot.ko.md)이 소유한다. Turn state, scope와 ambient context hook은
runtime private type이며 installed public header에 선언하지 않는다.

### 7.6 설치 header 제외 규칙

정식 public type 카탈로그에 없는 runtime state, [snapshot](../../../../01-glossary.ko.md#snapshot), access와 implementation type은 설치되는
header에 선언하거나 노출하지 않는다. Application이 구현 세부 이름을 include하거나 forward
declaration으로 참조해야 하는 구성을 공개 계약으로 인정하지 않는다.

---
<!-- framework-adapter-nav:bottom:start -->
[스펙 목차](README.ko.md) | [이전: C++ 시스템 구조](../01-system-structure.ko.md) | [다음: C++ HTTP Hosting](../60-http-hosting.ko.md)
<!-- framework-adapter-nav:bottom:end -->
