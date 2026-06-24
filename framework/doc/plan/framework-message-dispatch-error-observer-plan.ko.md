# Framework message dispatch error observer 계획

> 이 문서는 구현 전 계획이다. 현재 공개 계약이 아니며, 구현과 회귀 테스트가 끝난 뒤
> 언어별 정식 spec/guide 문서에 나누어 반영한다.

## 목적

framework runtime이 등록되지 않은 메시지나 처리 실패를 만났을 때 모든 언어에서 같은 정책으로
처리한다. request는 호출자가 응답을 기다리는 계약이므로 실패 원인과 관계없이 error reply를
반환해야 한다. 서버 쪽에서는 framework 기본 로그와 metric으로 실패를 볼 수 있어야 하며, 사용자가
원하면 전역 observer를 등록해 디버깅, 운영 모니터링, 외부 알림 시스템으로 연결할 수 있어야 한다.

이 기능은 dispatch 제어 기능이 아니라 관측 기능이다. observer가 등록되지 않아도 framework 기본
동작은 변하지 않아야 하고, observer 실패가 message 처리나 error reply 전송을 깨면 안 된다.

## 적용 범위

적용 대상은 framework가 있는 모든 언어다.

| 언어 | 적용 범위 |
|------|-----------|
| `.NET` | channel, route mesh channel, SPOT route, SPOT subscription, SPOT actor dispatch |
| Java/Kotlin | Spring Boot channel, route, SPOT route, SPOT subscription, SPOT actor dispatch |
| Node/NestJS | channel, route, SPOT route, SPOT subscription, SPOT actor dispatch |
| C++ | channel, route, SPOT runtime, session relay/session dispatch 중 framework가 소유한 dispatch 경로 |

stream connector의 inbound observer는 별도 기능이다. 이 계획은 framework message dispatch 단계의
오류 관측만 다룬다.

## 현재 상태 요약

이 절은 2026-06-20 checkout 기준이다.

| 영역 | 현재 상태 | 문제 |
|------|-----------|------|
| `.NET` channel request | handler가 없으면 `HandlerNotFound` error reply를 보내고 `ZLinkHandlerMissing` 로그와 metric을 기록한다. | 기본 방향은 맞다. 다만 dispatch option의 `Request` 값이 실제 분기 정책으로 쓰이지 않고 항상 reply error로 동작한다. |
| `.NET` channel send/publish | handler가 없으면 drop하고 message-flow 로그와 metric을 기록한다. | SPOT subscription/actor와 관측 방식이 다르다. |
| `.NET` SPOT route | request는 error reply, send는 warning drop을 수행한다. | channel과 같은 관측 경로를 사용하지만 전역 observer는 없다. |
| `.NET` SPOT subscription | topic/packet이 맞지 않으면 ignore counter만 증가한다. | 사용자가 표준 로그, metric, observer로 알기 어렵다. |
| `.NET` SPOT actor | handler가 없으면 send는 조용히 무시되고 request는 일부 경로에서 예외가 된다. | request error reply와 서버 로그 정책이 일관되지 않다. |
| Java/Kotlin | `ZLinkUnhandledDispatchOptions`는 있으나 runtime dispatch 경로에서 미등록 send/publish/route/spot/actor 대부분이 조용히 return/continue 한다. | option과 실제 동작이 연결되어 있지 않고 사용자가 알기 어렵다. |
| Node/NestJS | dispatch option 타입은 있으나 runtime에 거의 연결되어 있지 않다. channel route request 일부만 error reply를 보낸다. | request 미등록이 비구조화된 promise rejection으로 흐르거나 one-way 메시지가 조용히 사라진다. |
| C++ | framework 문서는 `.NET` parity를 목표로 하지만 언어별 dispatch failure 표면이 아직 공통 observer 계약으로 고정되어 있지 않다. | 회귀 기준과 공개 API가 언어별로 다를 수 있다. |

## 목표 정책

### 1. request 실패는 항상 error reply

아래 경우 request는 반드시 error reply를 보낸다.

| 실패 원인 | reply error kind |
|-----------|------------------|
| handler 없음 | `HandlerNotFound`, `RouteHandlerNotFound`, `ActorDispatchHandlerNotFound` 중 표면에 맞는 값 |
| payload decode 실패 | `PayloadDecodeFailed` |
| handler 실행 예외 | 기존 framework exception mapping 규칙에 따른 error reply |
| invalid request frame | `InvalidFrame` 또는 각 언어의 기존 protocol error kind |

request reply 경로가 물리적으로 없는 경우에는 서버 로그와 observer event에 `Action=Drop`과
`Reason=ReplyPathMissing`을 남긴다. 하지만 framework가 request로 인식했고 reply path가 존재하는
경로에서는 silent drop을 허용하지 않는다.

actor request도 같은 원칙을 따른다. remote actor request처럼 reply path가 있는 경우에는 error
reply를 보내고, 같은 process 안의 local actor call처럼 reply frame이 없는 경우에는 caller future나
promise를 framework exception으로 완료한다. 두 경우 모두 서버 Error 로그와 observer event를 남긴다.

### 2. one-way 메시지는 drop하되 관측 가능해야 함

send, publish, actor send, subscription은 reply path가 없으므로 error reply를 만들지 않는다. 대신
framework 기본 로그와 metric을 남기고 observer event를 발행한다.

| message kind | 기본 동작 | 기본 로그 레벨 |
|--------------|-----------|----------------|
| request 실패 | error reply | Error |
| route request 실패 | error reply | Error |
| actor request 실패 | reply path가 있으면 error reply, local call이면 caller-visible error | Error |
| send handler 없음 | drop | Warning |
| route send handler 없음 | drop | Warning |
| actor send handler 없음 | drop | Warning |
| publish/subscription handler 없음 | drop | Debug |
| invalid frame | request면 error reply, 아니면 drop | request Error, one-way Warning |

publish와 subscription은 관심 없는 메시지가 정상일 수 있으므로 기본 로그 레벨을 Debug로 둔다.
운영자가 더 강하게 보고 싶으면 기존 diagnostics/message-flow 설정으로 Warning 이상을 선택할 수
있게 한다.

### 3. 전역 observer 하나만 공개

1차 구현은 전역 observer만 제공한다. channel별, spot별 observer는 넣지 않는다. event payload에
channel, topic, spot, actor, source 정보를 넣어 사용자가 observer 내부에서 필터링하게 한다.

이렇게 하면 다음 정책 질문을 만들지 않는다.

- global observer와 local observer를 둘 다 호출할지
- local observer가 global observer를 대체할지
- 같은 사건을 중복 report할 때 알림 중복을 어떻게 막을지

필요성이 실제로 확인되면 이후 버전에서 local observer를 추가한다.

## 공통 public 계약 초안

이름은 언어별 관례를 따르되 의미는 동일해야 한다.

| 개념 | `.NET` | Java/Kotlin | Node/NestJS | C++ |
|------|--------|-------------|-------------|-----|
| observer interface | `IZLinkMessageDispatchErrorObserver` | `ZLinkMessageDispatchErrorObserver` | `ZLinkMessageDispatchErrorObserver` | `message_dispatch_error_observer_t` |
| event object | `ZLinkMessageDispatchErrorEvent` | `ZLinkMessageDispatchErrorEvent` | `ZLinkMessageDispatchErrorEvent` | `message_dispatch_error_event_t` |
| 등록 위치 | `ConfigureDispatch()` | `configureDispatch()` | `zlinkFramework().configureDispatch()` | `options.configure_dispatch()` |

### Event 필드

event는 불변 snapshot이어야 한다. observer가 값을 바꿔도 dispatch 흐름과 reply 생성에 영향을 주지
않는다.

| 필드 | 설명 |
|------|------|
| `Surface` | `Channel`, `RouteMeshChannel`, `SpotRoute`, `SpotSubscription`, `SpotActor`, `StreamSession` |
| `MessageKind` | `Request`, `Send`, `Publish`, `ActorRequest`, `ActorSend` |
| `Reason` | `HandlerMissing`, `PayloadDecodeFailed`, `HandlerException`, `InvalidFrame`, `ReplyPathMissing` |
| `Action` | `ReplyError`, `Drop` |
| `PacketName` | packet/message 이름. 알 수 없으면 빈 값이 아니라 `null`/optional로 표현한다. |
| `ChannelName` | channel 또는 route mesh 이름 |
| `Topic` | publish/subscription topic |
| `SpotRid` | SPOT routing id |
| `ActorId` | actor id |
| `SourceRid` | routing source id |
| `CorrelationId` | envelope correlation id 또는 request sequence |
| `Exception` | decode 실패나 handler 예외. handler 없음처럼 예외 객체가 없는 경우는 비워 둔다. |

필드 이름은 언어 관례에 맞게 바꿀 수 있지만, 정보 손실은 없어야 한다.
`StreamSession` surface는 framework가 소유한 session packet dispatcher에서만 사용한다. stream
connector의 inbound observer나 transport receive 관찰은 이 계획의 대상이 아니다.

### 언어별 public interface 초안

등록 표면은 기존 framework fluent/builder 표면에 붙인다. 새 API 때문에 channel, SPOT, handler 등록
방식을 바꾸지 않는다.

`.NET`:

```csharp
public interface IZLinkDispatchOptions
{
    IZLinkDispatchOptions SetMessageDispatchErrorObserver<TObserver>()
        where TObserver : class, IZLinkMessageDispatchErrorObserver;

    IZLinkDispatchOptions SetMessageDispatchErrorObserver(
        IZLinkMessageDispatchErrorObserver observer);
}

public interface IZLinkMessageDispatchErrorObserver
{
    ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken);
}

public sealed record ZLinkMessageDispatchErrorEvent(
    ZLinkDispatchErrorSurface Surface,
    ZLinkDispatchMessageKind MessageKind,
    ZLinkDispatchErrorReason Reason,
    ZLinkDispatchErrorAction Action,
    string? PacketName,
    string? ChannelName,
    string? Topic,
    string? SpotRid,
    string? ActorId,
    string? SourceRid,
    string? CorrelationId,
    Exception? Exception);
```

Java:

```java
public interface ZLinkDispatchOptions {
    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        Class<? extends ZLinkMessageDispatchErrorObserver> observerType);

    ZLinkDispatchOptions setMessageDispatchErrorObserver(
        ZLinkMessageDispatchErrorObserver observer);
}

public interface ZLinkMessageDispatchErrorObserver {
    CompletionStage<Void> onDispatchError(
        ZLinkMessageDispatchErrorEvent error);
}

public record ZLinkMessageDispatchErrorEvent(
    ZLinkDispatchErrorSurface surface,
    ZLinkDispatchMessageKind messageKind,
    ZLinkDispatchErrorReason reason,
    ZLinkDispatchErrorAction action,
    String packetName,
    String channelName,
    String topic,
    String spotRid,
    String actorId,
    String sourceRid,
    String correlationId,
    Throwable exception) {
}
```

Kotlin은 Java interface를 그대로 사용할 수 있게 하되, 필요하면 확장 함수만 얇게 제공한다.

```kotlin
fun ZLinkDispatchOptions.messageDispatchErrorObserver(
    observer: ZLinkMessageDispatchErrorObserver
): ZLinkDispatchOptions
```

Node/NestJS:

```ts
export interface ZLinkFrameworkOptions {
  configureDispatch(): ZLinkDispatchOptionsBuilder;
}

export interface ZLinkDispatchOptionsBuilder {
  setMessageDispatchErrorObserver(
    observerType: Type<ZLinkMessageDispatchErrorObserver>
  ): this;
}

export interface ZLinkMessageDispatchErrorObserver {
  onDispatchError(error: ZLinkMessageDispatchErrorEvent): Promise<void> | void;
}

export interface ZLinkMessageDispatchErrorEvent {
  readonly surface: ZLinkDispatchErrorSurface;
  readonly messageKind: ZLinkDispatchMessageKind;
  readonly reason: ZLinkDispatchErrorReason;
  readonly action: ZLinkDispatchErrorAction;
  readonly packetName?: string;
  readonly channelName?: string;
  readonly topic?: string;
  readonly spotRid?: string;
  readonly actorId?: string;
  readonly sourceRid?: string;
  readonly correlationId?: string;
  readonly error?: unknown;
}
```

C++:

```cpp
class dispatch_options_t
{
  public:
    dispatch_options_t &set_message_dispatch_error_observer(
      std::shared_ptr<message_dispatch_error_observer_t> observer);

    dispatch_options_t &set_message_dispatch_error_observer(
      std::function<void(const message_dispatch_error_event_t &)> observer);
};

class message_dispatch_error_observer_t
{
  public:
    virtual ~message_dispatch_error_observer_t () = default;
    virtual void on_dispatch_error (
      const message_dispatch_error_event_t &error) = 0;
};

struct message_dispatch_error_event_t
{
    dispatch_error_surface_t surface;
    dispatch_message_kind_t message_kind;
    dispatch_error_reason_t reason;
    dispatch_error_action_t action;
    std::optional<std::string> packet_name;
    std::optional<std::string> channel_name;
    std::optional<std::string> topic;
    std::optional<std::string> spot_rid;
    std::optional<std::string> actor_id;
    std::optional<std::string> source_rid;
    std::optional<std::string> correlation_id;
    std::exception_ptr exception;
};
```

enum 이름은 위 예시를 기준으로 언어별 관례에 맞게 작성한다. enum 값은 Event 필드 표의 값을
그대로 포함해야 한다.

### 등록 예시

`.NET` 예시:

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.ConfigureDispatch()
        .SetMessageDispatchErrorObserver<MyDispatchErrorObserver>();

    options.AddClientServerChannel("api")
        .EnableServer("tcp://127.0.0.1:5010");
});
```

Java 예시:

```java
@EnableZLinkFramework
public class ZLinkConfig implements ZLinkFrameworkConfigurer {
    @Override
    public void configure(ZLinkFrameworkOptions options) {
        options.configureDispatch()
            .setMessageDispatchErrorObserver(MyDispatchErrorObserver.class);

        options.addClientServerChannel("api")
            .enableServer("tcp://127.0.0.1:5010");
    }
}
```

Node 예시:

```ts
const framework = zlinkFramework();

framework.configureDispatch()
  .setMessageDispatchErrorObserver(MyDispatchErrorObserver);

framework.addClientServerChannel('api')
  .enableServer('tcp://127.0.0.1:5010');

ZLinkModule.forRoot(framework);
```

위 Node 예시는 to-be builder 모양이다. 구현할 때 `configureDispatch()`는 기존 `codecs()`처럼
세부 builder를 반환하되, root `zlinkFramework()` builder와 함께 쓸 수 있어야 한다.

C++ 예시:

```cpp
app.add_zlink_framework([](auto &options) {
  options.configure_dispatch()
    .set_message_dispatch_error_observer(
      std::make_shared<my_dispatch_error_observer>());

  options.add_client_server_channel("api")
    .enable_server("tcp://127.0.0.1:5010");
});
```

## Observer 실행 정책

observer는 request reply와 기본 로그/metric 이후에 실행한다.

1. 실패를 감지한다.
2. request이면 error reply를 만든다.
3. framework 기본 로그와 metric을 기록한다.
4. 불변 event snapshot을 만든다.
5. observer에 event를 전달한다.

observer callback은 native receive path에서 직접 실행하지 않는다. 구현은 언어별 runtime에 맞게
아래 조건을 만족해야 한다.

- event snapshot은 원본 `Message`, `Received`, native frame을 소유하지 않는다.
- observer 실패는 별도 error sink 또는 내부 debug 로그로만 남기고 dispatch를 실패시키지 않는다.
- observer 전달은 framework executor, serial executor, 또는 bounded queue를 통해 receive path에서
  분리한다.
- bounded queue를 쓰는 구현은 queue overflow 때 가장 최근 event를 drop하고 counter를 증가시킨다.
  이미 queue에 들어간 event를 밀어내지 않는다.
- shutdown 시에는 짧은 drain 기회를 주되 shutdown을 무기한 막지 않는다.

## 로그와 metric 정책

기본 로그는 observer 등록 여부와 무관하게 framework가 남긴다.

| Reason | Request 로그 | One-way 로그 |
|--------|--------------|--------------|
| `HandlerMissing` | Error | send/actor-send Warning, publish/subscription Debug |
| `PayloadDecodeFailed` | Error | Warning |
| `HandlerException` | Error | Error |
| `InvalidFrame` | Error | Warning |
| `ReplyPathMissing` | Error | 해당 없음 |

metric 이름은 언어별 기존 체계를 우선한다. `.NET`은 현재 `zlink.messages.handler_missing`,
`zlink.messages.dropped`, `zlink.messages.reply_error`가 있으므로 이를 확장한다. Java, Node, C++은
동일한 의미의 counter를 추가한다. 언어별 metric backend가 아직 없으면 runtime 내부 counter나
테스트 전용 sink로 회귀 테스트가 같은 의미를 검증할 수 있게 한다.

## 구현 단계

### Stage 0. 현재 동작 재현

각 언어에서 현재 미등록 dispatch 동작을 테스트로 먼저 재현한다. 이 단계의 목적은 개선 전 silent
drop 위치를 명확히 드러내는 것이다. 이 테스트는 기존 동작을 보존하기 위한 테스트가 아니다. 목표
정책을 구현하면서 기대값을 error reply, 로그, metric, observer event 기준으로 바꾼다.

- `.NET`: 기존 `UnhandledDispatchPolicyTests`를 확장해 SPOT subscription/actor 누락 경로를 포착한다.
- Java/Kotlin: channel send/publish, route request/send, SPOT route, subscription, actor missing handler를 테스트로 재현한다.
- Node/NestJS: channel request의 promise rejection 경로, route request error reply, send/publish silent return, SPOT subscription/actor 경로를 재현한다.
- C++: 현재 framework dispatch failure 표면을 contract/unit test로 목록화한다.

### Stage 1. 공통 모델 추가

각 언어에 event enum, event object, observer interface, 등록 옵션을 추가한다.

- 새 public API는 전역 observer 등록만 제공한다.
- 기존 unhandled dispatch option과 충돌하지 않게 한다.
- request `ReplyError`는 선택 정책이 아니라 기본 보장으로 둔다.
- one-way의 로그 레벨은 기존 diagnostics 설정과 연결한다.

### Stage 2. `.NET` 구현

`.NET`은 기존 message-flow logger와 telemetry를 재사용한다.

- channel request/send/publish 경로에 observer event를 붙인다.
- route mesh request/send 경로에 observer event를 붙인다.
- SPOT route request/send 경로에 observer event를 붙인다.
- SPOT subscription의 ignore counter 경로를 `HandlerMissing` event로 승격한다.
- SPOT actor request/send missing handler 경로를 error reply 또는 structured failure로 맞춘다.
- observer callback 실패를 `ZLinkRuntimeErrorSink`와 분리해 원래 dispatch를 깨지 않게 한다.

### Stage 3. Java/Kotlin 구현

Java는 이미 있는 `ZLinkUnhandledDispatchOptions`를 실제 runtime dispatch 경로와 연결한다.

- request missing handler는 가능한 모든 request path에서 error reply를 반환한다.
- one-way missing handler는 로그, metric, observer event를 남긴다.
- Spring Boot 등록 표면에서 observer bean/class 등록을 지원한다.
- Kotlin wrapper는 Java surface를 그대로 노출하거나 Kotlin idiom의 얇은 wrapper만 제공한다.
- actor/session 경로에서 handler 없음과 actor 없음을 구분해 event reason을 기록한다.

### Stage 4. Node/NestJS 구현

Node는 현재 `ZLinkUnhandledDispatchOptions` 계약과 문서가 맞지 않으므로 정리한다.

- request missing handler가 fire-and-forget promise rejection으로 새지 않도록 central dispatch error path를 만든다.
- route request missing handler는 기존 error reply를 유지하고 observer event를 추가한다.
- send/publish missing handler는 silent return 대신 로그, metric, observer event를 남긴다.
- SPOT subscription은 packetName 필터를 명확히 하고 미등록 packet을 observer event로 보고한다.
- SPOT actor request/send missing handler는 request면 error reply 또는 caller rejection, send면 warning drop으로 맞춘다.
- NestJS provider 기반 observer 등록을 지원한다.

### Stage 5. C++ 구현

C++은 public 표면이 `.NET` parity를 따라가도록 한다.

- `configure_dispatch` 아래 observer 등록 API를 추가한다.
- channel/route/SPOT/session relay dispatch failure를 공통 event로 변환한다.
- observer callback은 framework executor 또는 bounded queue를 통해 호출한다.
- callback exception이 C++ dispatch loop를 벗어나지 않게 한다.
- CTest label은 기존 `framework-zlink-*` 체계에 맞춘다.

### Stage 6. Cross-language parity 점검

각 언어 구현 뒤 다음 항목을 표로 맞춘다.

- request missing handler가 error reply를 반환하는가
- request decode failure가 error reply를 반환하는가
- request handler exception이 error reply를 반환하는가
- send/publish/subscription/actor-send missing handler가 drop되지만 관측 가능한가
- observer 미등록 시에도 기본 로그/metric이 남는가
- observer 등록 시 event가 한 번만 전달되는가
- observer 실패가 dispatch/reply를 깨지 않는가

### Stage 7. Codex 에이전트 적용 완료 리뷰

구현, 회귀 테스트, POSD 리팩토링, 정식 문서 반영이 끝난 뒤 Codex 에이전트로 이 계획 문서의 모든
항목이 실제 checkout에 반영되었는지 리뷰한다. 리뷰는 단순 요약이 아니라 적용 누락, 언어별 정책
불일치, 테스트 공백, 문서와 구현의 불일치를 찾는 검증 단계다.

리뷰 요청에는 이 문서 경로와 적용된 코드/문서 범위를 함께 전달한다. Codex 에이전트는 아래 항목을
반드시 확인해야 한다.

- 모든 적용 언어에 전역 dispatch error observer가 추가되었는가
- request 실패가 error reply 또는 caller-visible error로 끝나는가
- one-way 미등록 메시지가 silent drop으로 남지 않는가
- 기본 로그와 metric이 observer 등록 여부와 무관하게 남는가
- observer 실패가 dispatch, reply, shutdown을 깨지 않는가
- DERR 회귀 테스트가 각 언어에 실제로 추가되었는가
- 정식 spec, guide, internals 문서가 구현 기준으로 갱신되었는가
- POSD 기반 리팩토링 점검 결과가 코드 구조에 반영되었는가

리뷰에서 누락이나 불일치가 나오면 바로 수정하고 같은 범위를 다시 리뷰한다. Codex 에이전트가
"계획 문서의 모든 내용이 적용되었다"고 판단할 때까지 이 수정과 재리뷰를 반복한다. 이 리뷰가 통과하기
전에는 작업을 완료로 보지 않는다.

## 회귀 테스트 목록

### 공통 회귀 시나리오

| ID | 시나리오 | 기대값 |
|----|----------|--------|
| DERR-001 | channel request handler 없음 | client가 error reply를 받고 서버는 Error 로그와 observer event를 남긴다. |
| DERR-002 | route request handler 없음 | client가 error reply를 받고 서버는 Error 로그와 observer event를 남긴다. |
| DERR-003 | SPOT route request handler 없음 | caller가 error reply를 받고 서버는 Error 로그와 observer event를 남긴다. |
| DERR-004 | actor request handler 없음 | caller가 error reply 또는 framework exception을 받고 서버는 Error 로그와 observer event를 남긴다. |
| DERR-005 | request payload decode 실패 | error reply, Error 로그, observer event가 모두 발생한다. |
| DERR-006 | request handler 예외 | error reply, Error 로그, observer event가 모두 발생한다. |
| DERR-007 | channel send handler 없음 | 메시지는 drop되고 Warning 로그와 observer event가 발생한다. |
| DERR-008 | route send handler 없음 | 메시지는 drop되고 Warning 로그와 observer event가 발생한다. |
| DERR-009 | publish/subscription handler 없음 | 메시지는 drop되고 Debug 로그 또는 metric과 observer event가 발생한다. |
| DERR-010 | actor send handler 없음 | 메시지는 drop되고 Warning 로그와 observer event가 발생한다. |
| DERR-011 | observer callback 예외 | 원래 error reply/drop 동작은 성공하고 observer 실패만 별도 sink에 남는다. |
| DERR-012 | observer 전달 경로 포화 | dispatch는 막히지 않는다. bounded queue 구현은 overflow counter가 증가한다. |
| DERR-013 | observer 미등록 | 기본 로그와 metric은 계속 발생한다. |
| DERR-014 | event payload | packet, surface, kind, reason, action, channel/topic/actor/source 정보가 손실되지 않는다. |
| DERR-015 | one-way handler 예외 | Error 로그와 observer event가 발생하고 dispatch loop는 계속 동작한다. |
| DERR-016 | invalid frame | request면 error reply, one-way면 drop으로 끝나며 로그와 observer event가 발생한다. |

### 언어별 테스트 위치

| 언어 | 테스트 위치 |
|------|-------------|
| `.NET` | `framework/languages/dotnet/tests/Zlink.Framework.UnitTests/Runtime/UnhandledDispatchPolicyTests.cs`, `framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Channels`, `framework/languages/dotnet/tests/Zlink.Framework.E2ETests/Spot` |
| Java/Kotlin | `framework/languages/java/zlink-framework-core/src/test`, `framework/languages/java/zlink-framework-core/src/integrationTest`, `framework/languages/java/zlink-framework-testkit/src/fakeBackendTest`, `framework/languages/java/zlink-framework-spring-boot-starter/src/test`, `framework/languages/java/zlink-framework-kotlin/src/test` |
| Node/NestJS | `framework/languages/node/test/contract` 의 `node:test` 기반 테스트. channel은 `channel-client.test.js`/`handler-runtime.test.js`, SPOT/actor는 `spot-manager.test.js`/`actor-manager.test.js`, NestJS 등록 표면은 `nestjs-module.test.js`에 추가한다. |
| C++ | `framework/languages/cpp/tests/Zlink.Framework.UnitTests`, `framework/languages/cpp/tests/Zlink.Framework.ContractTests` |

## 구현 뒤 정식 문서 반영 계획

구현 전에는 정식 spec 문서에 계약처럼 쓰지 않는다. 구현과 회귀 테스트가 끝난 뒤 아래 문서에 나누어
반영한다.

| 문서 | 반영 내용 |
|------|-----------|
| `framework/doc/framework/common/spec/framework-api.ko.md` | 공통 dispatch error observer 의미와 event 필드 |
| `framework/doc/framework/common/spec/async-execution-policy.ko.md` | observer callback 격리, bounded queue, 실패 격리 정책 |
| `framework/doc/framework/dotnet/spec/handler-interfaces.ko.md` | `.NET` public interface와 등록 예시 |
| `framework/doc/framework/dotnet/spec/aspnet-core-channel-messaging.ko.md` | channel request/send/publish failure 정책 |
| `framework/doc/framework/dotnet/spec/aspnet-core-spot.ko.md` | SPOT route/subscription/actor failure 정책 |
| `framework/doc/framework/dotnet/guide/09-monitoring.ko.md` | 운영 모니터링 예시 |
| `framework/doc/framework/java/spec/handler-interfaces.ko.md` | Java/Kotlin public interface와 Spring 등록 |
| `framework/doc/framework/java/spec/spring-boot-channel-messaging.ko.md` | channel failure 정책 |
| `framework/doc/framework/java/spec/spring-boot-spot.ko.md` | SPOT failure 정책 |
| `framework/doc/framework/java/guide/09-monitoring.ko.md` | 운영 모니터링 예시 |
| `framework/doc/framework/node/spec/handler-interfaces.ko.md` | Node/NestJS public interface 정정과 등록 예시 |
| `framework/doc/framework/node/spec/nestjs-channel-messaging.ko.md` | channel failure 정책 |
| `framework/doc/framework/node/spec/nestjs-spot.ko.md` | SPOT failure 정책 |
| `framework/doc/framework/node/spec/nestjs-monitoring.ko.md` | 운영 모니터링 예시 |
| `framework/doc/framework/cpp/spec/cpp-framework-interfaces.ko.md` | C++ public interface와 callback 등록 |
| `framework/doc/framework/cpp/spec/cpp-channel-messaging.ko.md` | channel failure 정책 |
| `framework/doc/framework/cpp/spec/cpp-spot.ko.md` | SPOT failure 정책 |
| 각 언어 `internals/regression-test-matrix.ko.md` | DERR 회귀 항목과 실제 테스트 이름 |

guide에는 내부 queue, native socket, frame 처리 세부를 넣지 않는다. guide는 사용자가 observer를
등록하고 event를 필터링하는 방법만 설명한다. 내부 구현 설명은 internals 또는 common execution
policy 문서에 둔다.

## POSD 기반 리팩토링 단계

각 언어 구현이 끝난 뒤 바로 문서 반영으로 넘어가지 않고 POSD 기준으로 점검한다.

### Red flag 점검

| 위험 신호 | 확인 질문 |
|-----------|-----------|
| 얕은 모듈 | observer API가 내부 dispatch 구조를 그대로 노출하지 않는가 |
| 정보 누출 | native frame, raw message ownership, transport socket 정보가 event에 새지 않는가 |
| 특수/범용 혼합 | channel, SPOT, actor 별 특수 처리가 공통 reporting 모듈 안에 과도하게 섞이지 않는가 |
| 시간적 분해 | reply, log, observer 순서가 여러 파일에 흩어져 중복 구현되지 않는가 |
| 중복 주석 | 코드가 말하는 내용을 반복하는 주석이 추가되지 않았는가 |
| 패스스루 메서드 | observer 전달만 하는 얕은 wrapper가 여러 계층에 생기지 않았는가 |

### 두 가지 설계 대안 재검토

구현 뒤에도 아래 두 선택을 다시 검토한다.

| 선택지 | 장점 | 단점 |
|--------|------|------|
| 전역 observer 하나 | API가 단순하고 중복 report 정책이 없다. | channel/spot별 등록을 원하는 사용자는 event 필터링을 직접 해야 한다. |
| 전역 + local observer | scope별 처리 코드가 자연스럽다. | 중복 호출, 우선순위, override 정책이 생긴다. |

1차 구현은 전역 observer 하나로 유지한다. local observer가 필요하다는 실제 사용 사례와 테스트가
생기기 전까지 API를 넓히지 않는다.

### 리팩토링 완료 조건

- request error reply 생성이 각 runtime에 흩어진 ad hoc 코드가 아니라 공통 helper를 통한다.
- log, metric, observer event 생성이 같은 event snapshot에서 파생된다.
- observer 실패 처리 경로가 dispatch 실패 처리 경로와 분리되어 있다.
- public event에는 사용자가 판단할 수 있는 context만 있고 내부 transport 세부는 없다.
- 언어별 API 이름은 관례에 맞지만 event 의미와 기본 정책은 같다.

## 완료 기준

이 계획은 아래 조건을 모두 만족해야 완료로 본다.

1. `.NET`, Java/Kotlin, Node/NestJS, C++ framework에 전역 dispatch error observer가 있다.
2. 모든 request 미등록/실패 경로가 error reply 또는 caller-visible error로 끝난다.
3. 모든 one-way 미등록 경로가 drop되더라도 로그, metric, observer event로 관측된다.
4. observer 미등록 시에도 framework 기본 로그/metric이 남는다.
5. observer 실패가 dispatch loop, request error reply, shutdown을 깨지 않는다.
6. DERR 회귀 테스트가 각 언어에 추가되고 언어별 regression matrix에 연결된다.
7. POSD 점검과 필요한 리팩토링이 끝난 뒤 정식 spec/guide/internals 문서가 구현 기준으로 갱신된다.
8. Codex 에이전트가 이 계획 문서의 모든 항목이 적용되었다고 리뷰할 때까지 누락 수정과 재리뷰를
   반복한다.
