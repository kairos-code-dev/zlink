[스펙 목차](../../../../../doc/spec/draft/README.ko.md)

[C++ 묶음](../../../doc/README.ko.md) | [HTTP Client Draft](./cpp-http-client.ko.md) | [HTTP Client Guide](../README.ko.md)

# Draft -- ZLink HTTP Client Coroutine Support For C++

> 이 문서는 **구현 전 초안**이다.
> 현재 `zlink::http_client`의 공개 계약이 아니며, 정식 spec으로 승격하기 전까지
> 구현 완료를 보장하지 않는다.
> 현재 구현은 HTTP 교환을 동기로 실행하고, `co_await`는 이미 완료된 결과를
> 소비하는 형태다. 이 문서는 HTTP client가 실제로 coroutine suspend/resume을
> 지원하기 위한 다음 구현 계약을 정리한다.

## 1. 목적

HTTP client coroutine 지원의 목표는 호출자가 `co_await` 문법을 쓰는 것만이 아니다.
요청을 기다리는 동안 호출 스레드를 멈추지 않고, 응답이 준비되면 정해진 실행 위치에서
coroutine을 다시 이어서 실행하게 만드는 것이다.

이 기능이 필요한 대표 상황은 두 가지다.

- client 쪽 성능 테스트에서 많은 HTTP 요청을 동시에 걸고, 응답이 올 때마다 작은
  coroutine 작업을 이어서 실행한다.
- server handler 안에서 외부 HTTP API를 호출한다. 이때 handler thread가 막히면 같은
  server runtime이 처리할 다른 요청까지 지연될 수 있으므로, server가 제공하는
  coroutine scheduler와 이어져야 한다.

따라서 HTTP client coroutine 지원은 다음을 보장해야 한다.

- `co_await request.submit<T>()`가 HTTP 응답을 기다리는 동안 호출 스레드를 점유하지 않는다.
- server runtime이 scheduler를 제공하면, HTTP client는 완료 후 resume을 그 scheduler에 맡긴다.
- 별도 scheduler 설정이 없더라도 coroutine 사용을 명시적으로 켠 client는 사용할 수 있는
  기본 scheduler를 가진다.
- public header에 Boost.Beast, Boost.Asio, OpenSSL 세부 타입을 드러내지 않는다.

## 2. 현재 구현과의 차이

현재 `submit_raw()`/`submit<T>()`는 `zlink::framework::task_t`를 반환하지만, HTTP 교환은
`submit` 호출 중 동기로 끝난다. 즉 `co_await`는 결과를 꺼내는 문법일 뿐이고, HTTP I/O가
진행되는 동안 호출 스레드는 멈춘다.

이 draft의 목표 상태는 다르다.

```cpp
auto response = co_await client
  .post("/games")
  .body(create_game_req_t{.name = "ranked-match-0611"})
  .submit<create_game_res_t>();
```

위 코드는 다음 순서로 동작해야 한다.

1. request builder가 HTTP 작업을 scheduler에 등록한다.
2. 현재 coroutine은 suspend된다.
3. HTTP 작업은 scheduler가 가진 실행 위치에서 진행된다.
4. 작업이 끝나면 결과를 task에 저장한다.
5. resume scheduler가 coroutine을 다시 실행한다.
6. `await_resume()`은 성공 값을 반환하거나 실패를 예외로 변환한다.

## 3. 설계 원칙

### 3.1 HTTP client는 기본 scheduler를 내장한다

HTTP client는 connector와 달리 외부 `dispatch()` 호출이 응답을 펌핑해 주지 않는다.
따라서 coroutine을 실제로 suspend/resume하려면 HTTP 작업을 실행하고 완료 후 continuation을
깨워 줄 기본 실행 주체가 필요하다.

기본 실행 주체는 HTTP client 내부의 Boost 기반 scheduler로 둔다. public header에는 Boost
타입을 노출하지 않고, private runtime 구현 안에서 `boost::asio::io_context` 또는
`boost::asio::thread_pool`을 사용한다.

인자가 없는 `.coroutines()`는 이 내부 scheduler를 사용한다.

```cpp
auto client = zlink::http_client::client_t::create("http://127.0.0.1:18080")
  .json()
  .coroutines()
  .build();
```

이 설정은 client 쪽 성능 테스트와 독립 실행 도구의 기본 경로다. 여러 client가 생겨도
worker thread를 중복해서 만들지 않도록 scheduler는 process 안에서 공유한다.

### 3.2 Server runtime은 필요할 때 resume 정책을 주입한다

server runtime 안에서 HTTP client를 사용할 때는 handler continuation이 어디에서 다시
실행되는지가 중요하다. HTTP 작업을 내부 scheduler에서 실행하더라도, 완료 후 handler
coroutine은 server runtime이 허용한 위치에서 이어져야 한다.

따라서 server 쪽 사용에서는 최소한 resume 정책을 주입할 수 있어야 한다.

```cpp
auto client = zlink::http_client::client_t::create("https://matchmaking.internal")
  .json()
  .coroutines(server_resume_scheduler)
  .build();
```

이때 HTTP 작업 실행은 HTTP client 내부 scheduler가 맡고, resume은 server scheduler가 맡는다.
중요한 계약은 handler thread에서 blocking HTTP 작업을 직접 실행하지 않고, continuation이
server runtime의 규칙에 맞는 위치에서 실행되는 것이다.

일반 blocking client는 지금처럼 `fetch<T>()`, `.result()` 흐름을 유지한다. 사용자가
coroutine 실행을 원한다는 뜻을 builder에서 명시해야 scheduler가 붙는다.

### 3.3 실행 위치와 resume 위치를 개념적으로 분리한다

HTTP 요청을 실제로 처리하는 위치와 coroutine을 다시 실행하는 위치는 같을 수도 있고
다를 수도 있다.

- client 성능 테스트에서는 하나의 기본 scheduler가 HTTP 실행과 resume을 모두 처리해도 된다.
- server runtime에서는 HTTP 실행은 내부 Boost scheduler나 별도 worker에서 하고, resume은
  server scheduler로 돌려보내야 할 수 있다.

public API에서도 아래 두 역할을 분리해서 둔다. 그래야 server 쪽에서 HTTP 실행은 기본
scheduler에 맡기고 resume만 server scheduler로 돌려보내는 흔한 구성이 애매해지지 않는다.

| 역할 | 설명 |
|------|------|
| execute scheduler | HTTP 요청 작업을 실행한다 |
| resume scheduler | 완료된 coroutine을 다시 실행한다 |

server가 두 역할을 모두 직접 제공할 수도 있지만, 기본 server 연동은 execute scheduler를
생략하고 resume scheduler만 주입하는 형태로 둔다.

## 4. Public API 후보

### 4.1 Scheduler 인터페이스

HTTP client public header에는 구체 runtime 타입을 노출하지 않는다. HTTP 작업 실행과
coroutine resume은 서로 다른 작은 추상 인터페이스로 표현한다.

```cpp
namespace zlink::http_client
{

class coroutine_execute_scheduler_t
{
  public:
    virtual ~coroutine_execute_scheduler_t() = default;

    virtual void execute(std::function<void()> work) = 0;
};

class coroutine_resume_scheduler_t
{
  public:
    virtual ~coroutine_resume_scheduler_t() = default;

    virtual void resume(std::coroutine_handle<> continuation) = 0;
};

} // namespace zlink::http_client
```

`execute`는 HTTP 작업을 실행한다. 이 작업은 현재 구현의 동기 HTTP exchange를 호출해도
된다. 따라서 server용 execute scheduler는 이 작업을 handler thread에서 직접 실행하지 않아야
한다.

`resume`은 완료된 coroutine을 다시 실행한다. server runtime scheduler는 이 메서드에서
server가 정한 실행 위치로 continuation을 넘긴다.

HTTP client 내부 기본 scheduler는 두 인터페이스를 모두 구현한다.

### 4.2 Builder 설정

builder에는 coroutine 사용 설정을 추가한다.

```cpp
client_builder_t& coroutines();
client_builder_t& coroutines(std::shared_ptr<coroutine_resume_scheduler_t> resume_scheduler);
client_builder_t& coroutines(std::shared_ptr<coroutine_execute_scheduler_t> execute_scheduler,
                             std::shared_ptr<coroutine_resume_scheduler_t> resume_scheduler);
```

규칙은 다음과 같다.

- `.coroutines()`는 HTTP client 내부 기본 scheduler를 execute와 resume에 모두 사용한다.
- `.coroutines(resume_scheduler)`는 HTTP 작업 실행에는 내부 기본 scheduler를 쓰고,
  coroutine resume은 caller가 제공한 scheduler에 맡긴다.
- `.coroutines(execute_scheduler, resume_scheduler)`는 두 역할을 모두 caller가 제공한
  scheduler에 맡긴다.
- scheduler 인자가 `nullptr`이면 `request_protocol_error`로 실패한다.
- coroutine 설정이 없는 client에서 `submit<T>()`를 `co_await`하면 현재 blocking 실행과
  같은 의미를 유지한다. 기존 코드를 갑자기 비동기 실행으로 바꾸지 않는다.
- coroutine 설정이 있는 client에서 `submit_raw()` 또는 `submit<T>()`를 호출하면 HTTP 작업을
  즉시 scheduler에 등록한다. 이 규칙은 callback submit과 `.result()`가 같은 task 완료 상태를
  기다릴 수 있게 하기 위한 것이다.

### 4.3 Request API

기존 API 이름은 유지한다.

```cpp
zlink::framework::task_t<raw_http_response_t> submit_raw() const;

template <typename T>
zlink::framework::task_t<http_response_t<T>> submit() const;
```

별도 `submit_async`를 추가하지 않는다. C++ caller는 반환 타입이 awaitable인 `task_t`를
이미 보고 있고, blocking 편의 함수는 `fetch<T>()`로 분리되어 있다. 이름을 늘리면 같은
작업을 부르는 방법만 많아진다.

다만 문서에서는 다음 차이를 명확히 적는다.

| client 설정 | `submit<T>()` 실행 의미 |
|-------------|-------------------------|
| coroutine 설정 없음 | 현재 구현처럼 호출 중 HTTP 작업을 동기 실행한다 |
| `.coroutines()` | 내부 기본 scheduler가 HTTP 작업과 resume을 모두 처리한다 |
| `.coroutines(resume)` | 내부 기본 scheduler가 HTTP 작업을 실행하고 custom scheduler가 resume한다 |
| `.coroutines(execute, resume)` | custom scheduler들이 HTTP 작업과 resume을 처리한다 |

## 5. 기본 Scheduler

기본 scheduler는 HTTP client private runtime 안에 둔다. 구현은 Boost.Asio 기반으로 한다.
public header에는 Boost scheduler 타입, executor 타입, `io_context`, thread pool 타입을
노출하지 않는다.

목표는 복잡한 server runtime을 HTTP client 안에 복제하는 것이 아니라, coroutine 사용을
명시한 client가 별도 설정 없이도 thread를 막지 않는 요청을 만들 수 있게 하는 것이다.

기본 scheduler 요구사항은 다음과 같다.

- process 안에서 공유된다.
- lazy하게 생성된다.
- HTTP 작업을 Boost scheduler queue에 넣고 worker thread가 실행한다.
- 최소 구현은 worker thread 1개로 시작한다.
- process 종료 전 정상 사용 중에는 public shutdown을 제공하지 않는다.
- HTTP 작업 실행 중 발생한 예외는 `request_failed` 성격의 task 실패로 저장한다.
- callback 실행 중 발생한 예외는 caller에게 다시 던지지 않는다. callback은 task 완료 뒤에
  호출되므로 task 결과를 바꾸지 않는다.
- scheduler thread가 HTTP client public object lifetime에 묶이지 않는다. client가 사라져도
  이미 등록된 작업이 보유한 request state는 작업이 끝날 때까지 살아 있어야 한다.

기본 scheduler는 `execute`와 `resume`을 모두 처리한다. HTTP 작업이 끝나면 같은 scheduler에
continuation resume을 등록한다. resume을 worker thread에서 직접 호출하지 않고 scheduler
queue에 다시 넣으면, 작업 완료 중 발생한 lock이나 stack 상태와 caller continuation 실행을
분리할 수 있다.

기본 scheduler는 명시 shutdown API를 public 계약으로 제공하지 않는다. process 종료 전까지
살아 있는 공유 객체로 두고, 정적 객체 소멸 순서에 의존하지 않도록 intentionally leaked
singleton 또는 동등한 lifetime 전략을 쓴다. 테스트에서 scheduler를 강제로 닫아야 하는
요구가 생기면 별도 test-only hook으로 다룬다.

성능 테스트에서 병렬성을 더 높이기 위해 나중에 worker 수를 설정할 수 있어야 한다. 다만
첫 public 계약에는 worker 수 설정을 넣지 않는다. 필요하면 내부 환경 설정이나 별도 factory를
추가한다.

## 6. Custom Scheduler

server 쪽에서는 custom resume scheduler가 기본 resume보다 우선이다.

custom scheduler는 보통 다음 중 하나로 구현된다.

- server runtime의 task queue에 `resume`을 등록한다.
- HTTP 작업은 HTTP client 내부 Boost scheduler나 별도 blocking worker pool에 넣고, 완료 후
  server runtime queue에 resume을 등록한다.
- 이미 async I/O runtime을 가진 server라면 HTTP client backend를 그 runtime에 붙인다.

HTTP client는 resume scheduler가 어떤 thread에서 continuation을 resume하는지 가정하지
않는다. 다만 같은 task의 continuation은 한 번만 resume되어야 한다.

custom scheduler의 `execute`와 `resume`은 호출자에게 예외를 전파하지 않아야 한다. HTTP
client 구현은 scheduler 호출을 감싼다. `execute` 등록 중 예외가 나면 task를 `closed` 실패로
완료한다. `resume` 중 예외는 task 결과를 바꿀 수 없으므로 삼킨다. scheduler 예외가 handler
thread 밖으로 새면 server runtime의 오류 경계가 무너질 수 있기 때문이다.

## 7. 내부 동작 초안

### 7.1 Task 상태와 resume 제어

coroutine 실행을 위해 task 내부에는 완료 상태와 continuation이 필요하다.

필수 상태는 다음과 같다.

- `result_t<T>` 저장 공간
- 완료 여부
- `std::coroutine_handle<>` continuation
- 완료 callback 목록
- callback 실행 scheduler
- mutex 또는 lock-free 동기화 수단

`await_ready()`는 결과가 이미 있으면 `true`를 반환한다. 결과가 없으면 `false`를 반환한다.

`await_suspend(handle)`는 continuation과 resume scheduler를 저장한다. 이후 HTTP 작업 완료
경로가 result를 저장하고 scheduler의 `resume(handle)`을 호출한다.

`await_resume()`은 저장된 result를 확인한다. 성공이면 값을 반환하고, 실패면 기존
`task_t` 규칙처럼 `framework_exception_t`를 던진다.

현재 `zlink::framework::task_t`는 completion 시점에 저장된 continuation을 즉시
`resume()`한다. 이 상태로는 server resume scheduler 계약을 만족할 수 없다. 구현 전에
아래 중 하나를 선택해야 한다.

| 선택지 | 설명 | 판단 |
|--------|------|------|
| scheduler-aware `framework::task_t` | task state가 resume scheduler를 저장하고 completion 시 scheduler에 resume을 맡긴다 | framework handler와 HTTP client가 같은 task 타입을 유지하므로 우선 검토한다 |
| HTTP client 전용 task | HTTP client가 자체 awaitable task를 반환한다 | framework task와 결과 소비 규칙이 갈라지므로 우선순위가 낮다 |

우선 선택은 scheduler-aware `framework::task_t`다. 기존 동기 완료 task는 scheduler 없이
즉시 완료되는 의미를 유지하고, scheduler가 붙은 task만 completion 시점에 scheduler로
resume을 위임한다.

### 7.2 Submit 흐름

coroutine 설정이 있는 client의 `submit_raw()`는 호출 즉시 다음 흐름을 시작한다.

1. request builder가 method, path, header, body provider, timeout을 request state로 복사한다.
2. task completion source를 만든다.
3. execute scheduler에 작업을 등록한다.
4. 작업은 현재 runtime의 동기 `execute(request)`를 호출한다.
5. 작업이 끝나면 task completion source에 result를 저장한다.
6. continuation이 있으면 resume scheduler가 `resume(continuation)`을 호출한다.

이 방식은 첫 구현에서 Boost.Beast async API로 runtime 전체를 다시 쓰지 않아도 된다.
HTTP I/O는 worker에서 blocking으로 실행되지만, caller coroutine은 blocking되지 않는다.
caller가 반환된 task에 `.result()`를 호출하면 현재 스레드는 기존처럼 결과가 올 때까지
blocking으로 기다린다.

### 7.3 Typed Submit 흐름

`submit<T>()`는 `submit_raw()` task를 await한 뒤 JSON decode를 수행한다.

decode 위치는 resume된 coroutine 안이다. 즉 raw HTTP 작업은 execute scheduler에서 수행되고,
typed decode와 caller continuation은 resume scheduler 정책을 따른다.

이 규칙은 server handler에서 중요하다. DTO decode 후 handler state를 만지는 코드는 server가
정한 실행 위치에서 이어져야 하기 때문이다.

### 7.4 Streaming 제약

`body_stream(provider)`는 provider가 호출되는 thread가 중요하다. coroutine scheduler 기반
실행에서는 provider가 execute scheduler의 worker thread에서 호출된다.

`download(sink)`도 sink가 execute scheduler의 worker thread에서 호출된다. sink에서 server
handler state를 직접 건드리면 안 된다. 필요하면 caller가 thread-safe queue나 server scheduler
post를 사용해야 한다.

이 제약은 문서에 명확히 적는다. streaming callback까지 server resume 위치로 옮기려면
streaming 전용 async 계약이 필요하다.

### 7.5 Callback submit

`submit<T>(callback)`은 coroutine resume과 별도 계약을 가진다. callback은 task completion을
관찰하는 소비 방식이므로, coroutine continuation처럼 자동으로 server resume scheduler에서
실행된다고 보장하지 않는다.

coroutine 설정이 있는 client에서는 callback도 resume scheduler에 등록해서 호출한다. 이렇게
해야 server handler 주변 코드가 callback 스타일을 쓰더라도 handler thread를 막지 않고,
완료 callback 실행 위치를 예측할 수 있다. callback 호출 중 예외가 나면 task 결과를 바꿀 수
없으므로 scheduler 구현은 예외를 삼키거나 error callback 정책으로 보고해야 한다. public
계약은 callback 예외를 caller에게 다시 던지지 않는 것으로 둔다.

이를 위해 task completion 상태는 coroutine continuation뿐 아니라 완료 callback도 scheduler에
등록할 수 있어야 한다. scheduler가 없는 기존 blocking task는 현재처럼 completion 호출
위치에서 callback을 바로 실행해도 된다.

## 8. 오류와 취소

### 8.1 Scheduler 설정 오류

다음은 request를 보내기 전에 `request_protocol_error`로 실패한다.

- `.coroutines(nullptr)`
- coroutine 설정이 필요한 API를 scheduler 없이 호출하도록 별도 API가 추가된 경우

기존 `submit<T>()`는 scheduler 설정이 없어도 동기 실행으로 동작하므로 설정 오류가 아니다.

### 8.2 작업 등록 실패

custom execute scheduler가 shutdown 중이거나 작업을 받을 수 없으면 task는 실패 결과로
완료된다. HTTP client 내부 기본 scheduler는 public shutdown API를 갖지 않으므로 정상 사용
중에는 이 오류를 만들지 않는다.

권장 error kind는 `closed`다. 메시지는 HTTP client scheduler가 작업을 받을 수 없다는 뜻을
명확히 적는다.

### 8.3 Timeout

request timeout은 queue 등록 시점을 시작점으로 둔다. 호출자가 보는 대기 시간과 timeout
의미가 일치해야 하기 때문이다.

이를 구현하려면 request state에 absolute deadline 또는 등록 시각을 저장해야 한다. worker가
작업을 시작할 때 남은 시간을 계산해서 현재 동기 HTTP runtime에 전달한다. worker가 작업을
시작하기 전에 이미 deadline이 지났으면 HTTP 교환을 시작하지 않고 timeout 실패로 task를
완료한다.

### 8.4 Cancellation

첫 구현 계약에는 caller cancellation을 넣지 않는다. C++ coroutine handle만으로는 공통
cancellation 모델을 정의하기 어렵고, server runtime마다 cancellation token 의미가 다르다.

취소가 필요해지면 별도 draft에서 다룬다.

## 9. Lifetime

coroutine request는 client와 request builder 임시 객체 수명에 의존하면 안 된다.

규칙은 다음과 같다.

- `request_builder_t`는 scheduler 작업 등록 전에 필요한 request state를 값으로 복사한다.
- runtime은 `shared_ptr`로 보유되어 작업 완료까지 살아 있어야 한다.
- body string, headers, query 결과, timeout은 작업 state가 소유한다.
- `body_stream(provider)`와 `download(sink)`는 caller가 넘긴 callable을 작업 state가 소유한다.
- custom scheduler 객체는 client runtime이 `shared_ptr`로 보유한다.

## 10. Server Runtime 연동

server framework가 HTTP client coroutine을 안전하게 사용하려면 scheduler adapter를 제공한다.

adapter는 아래 의미를 만족해야 한다.

```cpp
class server_http_client_resume_scheduler_t final
  : public zlink::http_client::coroutine_resume_scheduler_t
{
  public:
    void resume(std::coroutine_handle<> continuation) override;
};
```

- `resume`은 server runtime이 허용한 위치에서 continuation을 실행한다.
- server shutdown 중에는 server 코드가 새 HTTP request를 만들지 않아야 한다.
- 이미 완료된 request의 continuation은 task 결과를 바꿀 수 없으므로, server resume scheduler가
  drain queue나 shutdown-safe executor에서 한 번은 resume해야 한다.

HTTP client는 server runtime 내부 타입을 include하지 않는다. server framework 쪽 adapter가
HTTP client resume scheduler 인터페이스를 구현한다. HTTP 작업 실행은 기본적으로 HTTP client
내부 scheduler가 맡는다.

## 11. Client 성능 테스트 사용

성능 테스트는 기본 scheduler를 사용할 수 있다.

```cpp
auto client = zlink::http_client::client_t::create("http://127.0.0.1:18080")
  .json()
  .coroutines()
  .build();
```

테스트는 여러 coroutine을 만들어 동시에 request를 등록한다. 각 coroutine은 응답을 받으면
latency와 status를 기록하고 다음 요청을 보낸다.

성능 테스트에서 필요한 측정 항목은 다음과 같다.

- 총 요청 수
- 성공/실패 status
- request 등록부터 response 완료까지의 latency
- scheduler queue 대기 시간
- worker 실행 시간
- response decode 시간

기본 scheduler worker 수가 1개라면 처리량 측정에는 제한이 있다. 이 제한은 문서에 표시하고,
worker 수 설정은 별도 구현 순서에서 추가한다.

## 12. 구현 순서

구현 goal은 아래 순서로 진행한다. 각 단계가 끝날 때마다 현재 diff를 리뷰하고, 발견한
문서·테스트·API 누락을 같은 단계 안에서 반영한 뒤 다음 단계로 넘어간다.

### 12.1 사전 검토

1. 현재 `framework::task_t`, HTTP client `submit_raw()`/`submit<T>()`, callback submit,
   `fetch<T>()`, runtime `execute(request)`, CMake target, contract header test를 먼저 읽는다.
2. 현재 동작 중 유지해야 할 계약을 목록으로 고정한다. scheduler 없는 client의 blocking 의미,
   `fetch<T>()`의 blocking 의미, streaming provider/sink 호출 위치, public header의 Boost 비노출
   규칙은 유지 대상이다.
3. scheduler-aware `framework::task_t`와 HTTP client 전용 task 두 대안을 다시 비교한다.
   비교 기준은 public API 단순성, framework handler와의 호환성, callback 처리, contract header
   영향, 테스트 범위다.
4. 선택한 설계가 이 문서의 execute/resume 분리 계약을 만족하는지 확인한다. 만족하지 않으면
   구현 전에 이 draft를 먼저 수정한다.

### 12.2 POSD 기반 리팩토링 검토

1. 구현 전 red flag를 먼저 열거한다. 확인 대상은 얕은 scheduler wrapper, `submit_raw()`와
   `submit<T>()` 사이의 중복 상태 복사, task completion과 HTTP runtime의 정보 누출,
   callback과 coroutine continuation의 이중 완료 경로, timeout 계산의 중복이다.
2. 각 red flag가 어떤 POSD 원칙을 어기는지 적는다. 특히 깊은 모듈, 정보 은닉,
   복잡성을 아래로 내리는 원칙, 오류를 정의로 없애는 원칙을 기준으로 본다.
3. 비자명한 설계 결정마다 두 가지 이상 대안을 비교한다.
   - task resume 제어: scheduler-aware `framework::task_t` vs HTTP client 전용 task
   - scheduler API: execute/resume 분리형 vs 단일 scheduler
   - timeout 기준: queue 등록 시점 deadline vs worker 시작 시점 timeout
   - default scheduler lifetime: process lifetime singleton vs 명시 shutdown API
4. 선택한 대안이 caller가 알아야 할 설정과 순서를 줄이는지 확인한다. caller가 scheduler
   내부 queue, worker, Boost type, completion source를 알 필요가 있으면 설계를 다시 줄인다.
5. 구현 중 새 red flag가 생기면 즉시 정리한다. 단순 pass-through class, 순서만 나눈 helper,
   같은 request state를 여러 곳에서 따로 조립하는 코드는 그대로 두지 않는다.

### 12.3 Public 계약과 task 기반 구현

1. `framework::task_t` 또는 HTTP client task가 resume scheduler를 통해 continuation을 재개할 수
   있게 한다. 우선 선택은 scheduler-aware `framework::task_t`다.
2. `task_t` completion 상태가 callback도 scheduler에 등록할 수 있게 한다. scheduler 없는 기존
   task는 현재처럼 completion 호출 위치에서 바로 callback을 실행한다.
3. `coroutine_execute_scheduler_t`와 `coroutine_resume_scheduler_t` public 인터페이스를 추가한다.
4. `client_builder_t::coroutines()`, `coroutines(resume)`,
   `coroutines(execute, resume)`를 추가한다.
5. HTTP client runtime options에 execute scheduler와 resume scheduler 보유 필드를 추가한다.
6. contract header test에 새 public header와 scheduler 타입을 추가한다.

### 12.4 HTTP client runtime 구현

1. 기본 Boost 기반 scheduler를 private runtime 구현으로 추가한다. public header에는 Boost type을
   노출하지 않는다.
2. 기본 scheduler는 process lifetime 동안 공유되게 한다. public shutdown API는 추가하지 않는다.
3. `submit_raw()`가 scheduler 설정이 있는 경우 호출 즉시 request state를 복사하고 scheduler에
   작업을 등록하게 한다.
4. queue 등록 시점을 timeout 시작점으로 쓰도록 request state에 deadline을 저장한다.
5. worker가 작업을 시작할 때 deadline이 지났으면 HTTP 교환을 시작하지 않고 timeout 실패로
   task를 완료한다.
6. `submit<T>()`가 raw task await 후 typed decode를 수행하는 의미를 유지하는지 확인한다.
7. `submit<T>(callback)`이 coroutine 설정이 있는 client에서는 resume scheduler에서 callback을
   실행하도록 정리한다.
8. `fetch<T>()`와 `.result()`는 blocking convenience로 계속 동작하게 한다.
9. server framework adapter는 HTTP client public 인터페이스만 보고 별도 파일에서 구현한다.

### 12.5 테스트와 검증

1. scheduler 없는 client의 기존 테스트를 먼저 통과시킨다. 기존 blocking 의미가 바뀌면 실패로
   본다.
2. coroutine unit/e2e test를 추가한다. 호출 스레드가 응답 대기 중 막히지 않는지 검증한다.
3. custom resume scheduler test를 추가한다. continuation과 callback이 지정한 scheduler에서
   실행되는지 검증한다.
4. custom execute scheduler test를 추가한다. 작업 등록과 등록 실패(`closed`)가 계약대로
   동작하는지 검증한다.
5. queue deadline timeout test를 추가한다. queue에서 timeout이 지나면 HTTP 교환을 시작하지
   않아야 한다.
6. streaming upload/download test를 보강한다. provider와 sink가 execute scheduler worker에서
   호출된다는 제약을 확인한다.
7. server handler 안에서 custom resume scheduler를 통해 resume되는지 검증한다.
8. 성능 테스트에서 기본 scheduler를 사용해 여러 coroutine request를 동시에 실행한다.
9. 관련 label을 실제로 확인한다. 최소 검증은 `http-client-contract`, `http-client-unit`,
   `http-client-e2e`, `http-client-regression` label이다.

### 12.6 반복 리뷰와 마무리

1. 구현 후 diff를 POSD 기준으로 다시 리뷰한다. red flag가 남아 있으면 리팩토링하고 테스트를
   다시 돌린다.
2. public API 리뷰를 수행한다. caller가 Boost type, scheduler queue, task completion source,
   worker lifetime을 알 필요가 없는지 확인한다.
3. concurrency 리뷰를 수행한다. continuation/callback이 한 번만 실행되는지, task 완료와
   callback 등록의 race가 없는지, request state lifetime이 scheduler 작업 완료까지 유지되는지
   확인한다.
4. error 리뷰를 수행한다. scheduler 등록 실패, HTTP 실패, timeout, JSON decode 실패,
   callback 예외가 서로 다른 계약으로 보고되는지 확인한다.
5. 문서 리뷰를 수행한다. guide, draft, error 문서, contract test 기대가 구현과 일치하는지
   확인하고 누락을 반영한다.
6. 리뷰에서 이슈가 나오면 수정 후 12.5 검증으로 돌아간다. 추가 이슈가 없을 때만 완료로 본다.

## 13. 테스트 항목

최소 회귀 테스트는 다음을 포함한다.

- scheduler 없는 client는 기존 blocking 실행 의미를 유지한다.
- `.coroutines()` client는 default scheduler를 사용한다.
- `.coroutines(nullptr)`은 `request_protocol_error`로 실패한다.
- `.coroutines(resume)`은 내부 execute scheduler와 custom resume scheduler를 함께 사용한다.
- `.coroutines(execute, resume)`은 두 custom scheduler를 모두 사용한다.
- coroutine request가 await 중 호출 스레드를 막지 않는다.
- HTTP 작업 완료 후 continuation이 정확히 한 번 resume된다.
- HTTP status 실패, JSON decode 실패, timeout이 task 실패로 전달된다.
- custom execute scheduler와 custom resume scheduler가 각각 호출된다.
- custom resume scheduler가 continuation과 callback 실행 위치를 제어한다.
- queue에서 timeout이 지나면 HTTP 교환을 시작하지 않고 timeout으로 완료된다.
- 임시 builder에서 만든 request가 scheduler 작업 완료까지 안전하게 살아 있다.
- `body_stream(provider)` provider는 worker thread에서 호출된다는 제약을 문서와 테스트가
  함께 확인한다.
- `download(sink)` sink는 worker thread에서 호출된다는 제약을 문서와 테스트가 함께 확인한다.
- scheduler shutdown 중 작업 등록 실패가 `closed`로 보고된다.
- POSD 리뷰에서 발견한 red flag가 해소되었고, 남은 위험이 있으면 문서에 명시되어 있다.
- public header에 Boost.Beast, Boost.Asio, OpenSSL runtime type이 노출되지 않는다.
- task completion과 callback 등록 race가 재현 테스트로 막혀 있다.

## 14. 정식 문서 반영 기준

구현이 끝난 뒤에는 다음 문서를 함께 갱신한다.

- `http-client/doc/01-overview.ko.md`: 실행 모델에서 blocking-only 설명을 갱신한다.
- `http-client/doc/07-async-coroutines.ko.md`: scheduler 설정과 server handler 사용법을 추가한다.
- `http-client/doc/13-error-handling.ko.md`: scheduler 등록 실패와 shutdown 오류를 추가한다.
- `http-client/doc/draft/cpp-http-client.ko.md`: 정식 draft의 API 목록과 회귀 테스트 축을 갱신한다.

정식 spec 문서에는 구현과 테스트가 완료된 뒤 반영한다.
