# Stream Connector Inbound Observer 구현 계획

이 문서는 구현 전 초안이며 현재 공개 계약이 아니다.
아래 내용은 C++, Java, .NET, Node Stream Connector에 수신 메시지 관찰 기능을
추가하기 위한 계획이다. 정식 spec 문서와 각 언어 공개 API에 반영되기 전까지
응용은 이 동작에 의존하면 안 된다.

## 목적

Stream Connector를 사용하는 client code에서는 push, request 응답, 원격 error,
heartbeat 같은 수신 frame을 공통으로 기록하거나 측정하고 싶은 경우가 있다.
현재 `On(...)` handler는 packet 이름별 push 처리에 맞춰져 있어서 request 응답처럼
pending request에서 바로 소비되는 frame은 볼 수 없다.

이 기능은 handler 실행을 가로막거나 메시지를 바꾸기 위한 기능이 아니다.
수신 frame을 읽기 전용 정보로 관찰하고, connector의 정상 처리 흐름은 그대로
진행되게 하는 것이 목적이다.

## 비목표

- 수신 메시지를 버리거나 중간에서 처리를 끝내거나, retry하거나, 변환하는 기능은
  포함하지 않는다.
- observer가 reply를 만들거나 pending request 완료를 대신하지 않는다.
- observer가 payload 소유권을 가져가지 않는다.
- client connector에 DI container 등록 방식을 요구하지 않는다.
- observer를 connector options에 넣는 방식을 기본 API로 삼지 않는다.
- 서버 framework의 handler filter와 같은 API로 합치지 않는다.

## 용어

- inbound observation: connector가 수신한 frame에서 만든 읽기 전용 관찰 정보.
- observer: observation을 받아 logging, metrics, tracing 같은 부수 작업을 수행하는
  사용자 callback.
- pump path: transport에서 frame을 읽고 connector 내부 상태를 진행시키는 경로.

## 공통 계약 초안

각 언어는 이름 규칙은 언어 관례를 따르되 다음 의미를 동일하게 제공한다.

### Observation 필드

첫 구현에서는 payload 본문을 기본 필드로 제공하지 않는다. payload 본문은 크거나
수명이 짧을 수 있고, 관찰 기능 때문에 receive pump가 payload copy 비용을 항상
지불하면 안 되기 때문이다.

필수 필드:

- message kind: `Send`, `Request`, `Response`, `Error`, `Control`
- packet name
- codec
- request sequence. 없으면 null, optional, undefined 중 언어 관례에 맞는 값
- metadata
- payload byte length
- compressed 여부 또는 압축 flag
- received time. 가능하면 뒤로 가지 않는 clock 기준

선택 필드:

- payload preview. 옵션으로 정한 최대 byte 수만 copy해서 제공한다.
- remote endpoint. transport가 안정적으로 제공할 수 있을 때만 포함한다.

### Snapshot 규칙

observation은 observer가 바꿀 수 없는 값이어야 한다. 이 규칙은 payload뿐 아니라
metadata에도 적용된다.

- metadata는 언어별 read-only wrapper 또는 내부 copy로 제공한다.
- observer가 metadata 객체를 바꾸려 해도 connector dispatch, pending request 완료,
  handler가 보는 값은 바뀌지 않아야 한다.
- payload 본문은 기본으로 제공하지 않는다.
- payload preview를 제공할 때는 옵션으로 정한 최대 길이만 copy한다.
- preview buffer를 observer가 바꿔도 connector 내부 payload에는 영향을 주지 않아야 한다.

### 호출 시점

observer notification은 header decode와 frame 크기 검증이 끝난 뒤에 만든다.
request 응답도 관찰해야 하므로 pending request 완료보다 앞에서 observation을
생성해야 한다.

처리 순서:

```text
frame received
  -> decode header
  -> create inbound observation
  -> enqueue observer notification
  -> continue connector dispatch
       -> control handling
       -> pending request completion
       -> error callback
       -> packet handler or manual dispatch queue
```

다이어그램 내부는 영문만 사용한다. 위 흐름에서 중요한 점은 observer 실행이 아니라
observer notification 등록만 receive 경로에 있다는 점이다.

### Pump path 제약

observer callback은 pump path에서 직접 실행하지 않는다.

이 규칙이 필요한 이유:

- observer가 느리면 request 응답 완료가 늦어진다.
- heartbeat control 처리와 reconnect 판단이 지연될 수 있다.
- observer에서 connector API를 다시 호출하면 재진입 문제가 생길 수 있다.
- logging exporter, file write, network 전송 같은 작업은 수신 진행과 분리해야 한다.

따라서 각 언어 구현은 pump path에서 변경할 수 없는 snapshot을 만들고 별도 callback
queue 또는 task runner에 observer 실행을 맡긴다. observer 실패는 connector의
error reporting 경로로 전달하되 원래 수신 메시지 처리는 계속한다.

### Observer dispatch queue

observer 실행 queue는 receive pump를 막지 않아야 한다. 첫 구현은 bounded
non-blocking queue를 기본 정책으로 삼는다.

- queue가 가득 차면 해당 observation notification만 버린다.
- notification이 버려져도 원래 frame 처리, pending request 완료, handler dispatch는
  계속된다.
- overflow가 처음 발생했을 때는 connector error reporting 경로로 보고한다.
- 같은 overflow가 반복될 때 error reporting이 무한히 쌓이지 않도록 구현별 throttle
  또는 coalescing 정책을 둔다.
- queue 기본 크기는 1024개 notification으로 둔다.
- 테스트에서 overflow를 결정적으로 만들 수 있도록 queue 크기는 connector option으로
  조정할 수 있게 한다. 이 option은 observer 등록 방식이 아니라 connector 실행 정책이다.
- queue 크기 option 이름은 언어 관례를 따르되 `max inbound observer notifications`
  의미가 드러나야 한다.
- queue overflow 때문에 transport read, heartbeat, reconnect 처리가 block되면 안 된다.

### Observer 실패 처리

observer callback이 실패해도 원래 메시지 처리는 계속된다.

- 실패는 `observer-failed`에 해당하는 connector error로 보고한다.
- error reporting 중 다시 실패하면 그 실패는 삼키고 receive pump에는 전파하지 않는다.
- observer 실패 때문에 pending request를 실패 처리하지 않는다.
- observer 실패 때문에 `On(...)` handler 호출을 건너뛰지 않는다.
- 한 observer가 실패해도 같은 observation의 다른 observer는 계속 호출한다.

### Error code 정책

observer 기능은 기존 remote error, frame decode error, user callback error와 구분되는
error를 보고해야 한다.

- observer callback 실패는 `observer-failed` 의미의 error code 또는 error kind로 보고한다.
- observer queue overflow로 notification을 버릴 때는 `observer-dropped` 의미의 error code
  또는 error kind로 보고한다.
- 언어별 enum 이름은 관례를 따르되, 문서와 테스트에서는 위 두 의미를 분리해서 검증한다.
- 이 error는 관찰 기능의 실패를 알리는 진단 신호이며, 원래 수신 frame의 성공/실패 의미를
  바꾸지 않는다.

### Control frame 정책

control frame은 기본 관찰 대상에 포함한다. heartbeat 같은 control frame은 운영 로그에서
시끄러울 수 있으므로 observer 구현에서 kind 또는 packet name으로 걸러낼 수 있게 한다.

### 등록 방식

client connector는 DI를 사용하지 않는다. 또한 observer는 endpoint, timeout, codec 같은
정적 설정값보다 callback 등록에 가깝다. 따라서 각 언어의 기본 등록 방식은 생성된
connector instance에 observer를 붙이는 방식이다.

공통 예:

```text
connector = connector_factory.create(options)
registration = connector.observe_inbound(observer)
connector.connect()
```

첫 구현에서는 `connect` 전에 등록한 observer만 지원한다. 연결 후 등록은 실패시키거나,
언어별 결과 타입으로 invalid state를 돌려준다. 반환된 registration, disposable,
AutoCloseable을 해제하면 이후 observation을 받지 않는다.

연결 중 observer 추가/삭제가 필요하면 별도 기능으로 다시 설계한다. 이 경우 어떤 frame부터
관찰 대상에 들어가는지, 삭제 중 실행 중인 callback을 기다릴지 등을 따로 정해야 한다.

## .NET 계획

### 공개 API 초안

대상 패키지:

- `framework/languages/dotnet/src/Systems.Zlink.Stream.Connector`

추가 타입:

- `IZlinkStreamInboundObserver`
- `ZlinkStreamInboundObservation`
- 선택: `ZlinkStreamInboundObserver.Create(...)` delegate helper

사용 예:

```csharp
await using var connector = ZlinkStreamConnectorFactory.Create(options);

using var inboundLog = connector.ObserveInbound((observation, cancellationToken) =>
{
    logger.LogInformation(
        "inbound kind={Kind} name={Name} seq={RequestSeq} bytes={Bytes}",
        observation.Kind,
        observation.Name,
        observation.RequestSeq?.Value,
        observation.PayloadLength);
    return ValueTask.CompletedTask;
});

await connector.Connect.Async(cancellationToken);
```

### 구현 지점

- `IZlinkStreamConnector`에 `ObserveInbound(...)` 등록 API를 추가한다.
- `ZlinkStreamConnector` 내부 상태에 observer 목록과 observer dispatcher를 둔다.
- `ObserveInbound(...)`는 연결 전 상태에서만 성공한다.
- `ZlinkStreamReceiveDispatcher.DispatchPacketAsync(...)`에서 header decode 직후
  observation을 만들고 observer dispatcher에 넘긴다.
- observer callback은 기존 `ZlinkStreamTaskRunner` 또는 별도 bounded queue에서
  실행한다.
- `pending.TryComplete(...)`보다 observation 생성이 앞에 있어야 한다.

### .NET 테스트

추가 테스트:

- `Response` frame이 pending request를 완료하더라도 observer가 먼저 관찰한다.
- `Send` frame은 observer와 기존 `On(...)` handler 둘 다 도달한다.
- `Control` heartbeat frame도 observer 정책에 맞게 관찰된다.
- observer 예외가 원래 message dispatch와 request completion을 막지 않는다.
- observer callback은 receive path를 직접 막지 않는다. 느린 observer를 등록한 뒤
  request completion이 observer 완료를 기다리지 않는지 확인한다.
- 연결 후 `ObserveInbound(...)`가 실패하는지 확인한다.
- registration dispose 뒤 새 frame observation을 받지 않는지 확인한다.
- observer queue overflow가 원래 request completion을 막지 않고 drop error만 남기는지
  확인한다.
- metadata와 payload preview가 observer에 의해 바뀌어도 dispatch payload가 바뀌지
  않는지 확인한다.
- payload preview 옵션을 추가한다면 preview 길이 제한과 copy 여부를 검증한다.

문서:

- `framework/languages/dotnet/doc/spec`의 Stream Connector 문서에 정식 반영한다.
- guide에는 logging/metrics 예제를 두되, observer가 메시지를 막거나 바꾸지 못한다는
  설명을 함께 적는다.

## Java 계획

### 공개 API 초안

대상 패키지:

- `framework/languages/java/zlink-stream-connector`

추가 타입:

- `ZLinkStreamInboundObserver`
- `ZLinkStreamInboundObservation`

예:

```java
ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);

try (AutoCloseable inboundLog = connector.observeInbound(observation -> {
    logger.info(
        "inbound kind={} name={} seq={} bytes={}",
        observation.kind(),
        observation.name(),
        observation.requestSeq().orElse(null),
        observation.payloadLength());
    return CompletableFuture.completedFuture(null);
})) {
    connector.connect().await();
    connector.request(payload)
        .packetName("login")
        .submit()
        .toCompletableFuture()
        .join();
}
```

Java는 `ZLinkStreamConnectorOptions` record 생성자를 늘리지 않는다.

### 구현 지점

- `DefaultZLinkStreamConnector.dispatchInbound(...)`에서 header decode와 payload decode
  뒤 observation을 만든다.
- `KIND_RESPONSE` pending completion보다 앞에서 observer notification을 등록한다.
- observer 전용 bounded executor 또는 queue를 둔다. manual dispatch queue는 사용자
  handler 실행 시점과 연결되어 있으므로 observer에 재사용하지 않는다.
- `observeInbound(...)`는 연결 전 상태에서만 성공한다.
- observer 실패는 `publishError(...)`로 보고하고 원래 dispatch는 계속한다.

### Java 테스트

추가 테스트:

- response, error response, send, request, control frame 관찰.
- pending request completion이 observer 완료를 기다리지 않음.
- observer 예외가 request completion과 handler dispatch를 막지 않음.
- 연결 후 `observeInbound(...)`가 실패함.
- `AutoCloseable` close 뒤 새 frame observation을 받지 않음.
- observer queue overflow가 원래 request completion을 막지 않고 drop error만 남김.
- metadata와 payload preview가 observer에 의해 바뀌어도 dispatch payload가 바뀌지 않음.

문서:

- `framework/languages/java/doc/spec/stream-connector.ko.md`에 정식 계약을 반영한다.
- Java connector README 또는 guide에 DI 없이 connector instance에 등록하는 예제를 추가한다.

## Node 계획

### 공개 API 초안

대상 패키지:

- `framework/languages/node/packages/stream-connector`

추가 타입:

- `ZlinkStreamInboundObserver`
- `ZlinkStreamInboundObservation`

사용 예:

```ts
const connector = zlinkStreamConnectorFactory.create(options);

const inboundLog = connector.observeInbound(async observation => {
  logger.info({
    kind: observation.kind,
    name: observation.name,
    requestSeq: observation.requestSeq,
    payloadLength: observation.payloadLength
  });
});

await connector.connect();
```

### 구현 지점

- `ZlinkStreamConnector` interface에 `observeInbound(...)` 등록 API를 추가한다.
- `DefaultZlinkStreamConnector`에 observer 목록과 observer dispatch queue를 추가한다.
- `observeInbound(...)`는 연결 전 상태에서만 성공한다.
- `DefaultZlinkStreamConnector.dispatchFrame(...)`에서 `pendingRequests.resolve(...)`보다
  앞에 observation을 만든다.
- Node는 같은 event loop를 사용하므로 observer를 `await`하지 않는다.
  `queueMicrotask` 또는 internal promise chain으로 실행하고, 실패는 `publishError(...)`
  경로로 전달한다.
- observer가 많거나 느려도 receive loop가 observer 완료를 기다리지 않게 한다.

### Node 테스트

추가 테스트:

- response가 pending request로 소비되어도 observer가 호출된다.
- send frame에서 observer와 handler가 모두 호출된다.
- observer promise reject가 기존 dispatch를 막지 않고 error handler로 전달된다.
- observer가 느려도 request promise가 observer 완료를 기다리지 않는다.
- 연결 후 `observeInbound(...)`가 실패한다.
- returned disposable dispose 뒤 새 frame observation을 받지 않는다.
- observer queue overflow가 원래 request completion을 막지 않고 drop error만 남긴다.
- metadata와 payload preview가 observer에 의해 바뀌어도 dispatch payload가 바뀌지 않는다.

문서:

- `framework/languages/node/doc/spec` 또는 package README에 observer 등록 예제를 추가한다.
- `On(...)` handler와 observer의 차이를 설명한다.

## C++ 계획

### 공개 API 초안

대상 패키지:

- `framework/languages/cpp/connector/core`

추가 타입:

- `inbound_observation_t`
- `inbound_observer_t`

첫 구현은 생성된 connector에 observer를 붙이는 API만 제공한다.

```cpp
auto connector = connector_factory_t::create(options);

auto inbound_log = connector.observe_inbound([] (const inbound_observation_t &observation) {
    logger.info(observation.name);
});

connector.connect();
```

`observe_inbound(...)`는 연결 전 상태에서만 성공한다. 반환 객체가 살아 있는 동안만
observer가 유지된다. observer는 메시지 흐름을 바꾸지 못한다.

### 구현 지점

- `connector_runtime.hpp`의 `connector_state_t`에 observer 목록과 observer 실행 queue를
  추가한다.
- `connector_t`에 `observe_inbound(...)` 등록 API를 추가한다.
- `observe_inbound(...)`는 연결 전 상태에서만 성공한다.
- `try_take_inbound_frame(...)` 또는 `process_inbound_buffer(...)`에서 header와 packet
  정보를 바탕으로 observation을 만든다.
- response frame이 `completed_requests`로 이동하기 전에 observer notification을
  등록한다.
- `schedule_delivery(...)` 같은 기존 callback 분리 경로를 재사용할 수 있는지 확인한다.
- immediate dispatch 모드에서도 observer callback이 receive lock 안에서 실행되지
  않게 한다.

### C++ 테스트

추가 테스트:

- response frame 관찰과 pending request callback 완료 순서.
- manual dispatch 모드에서 observer가 `dispatch()` 호출을 기다리지 않는지, 또는
  의도한 별도 dispatch 정책을 정확히 따르는지 검증.
- immediate dispatch 모드에서 observer가 packet handler보다 먼저 notification을
  등록하지만 receive lock 안에서 handler처럼 직접 실행되지 않는지 검증.
- 연결 후 `observe_inbound(...)`가 실패하는지 검증.
- registration 객체 해제 뒤 새 frame observation을 받지 않는지 검증.
- observer queue overflow가 원래 request completion을 막지 않고 drop error만 남기는지
  검증.
- metadata와 payload preview가 observer에 의해 바뀌어도 dispatch payload가 바뀌지
  않는지 검증.
- observer callback 예외는 connector가 잡아서 `on_error`에 `observer-failed` 성격의
  error로 보고하고 원래 메시지 처리는 계속하는지 검증한다.
- payload preview를 제공하면 copy 크기 제한을 검증한다.

문서:

- `framework/languages/cpp/connector/doc/guide/05-receiving.ko.md`에 observer 사용 예제를
  추가한다.
- 정식 계약은 connector spec 문서로 승격할 때 별도 절로 분리한다.

## 샘플 반영 계획

Bingo와 TicTacToe 샘플은 observer 기능을 실제 client logging 예제로 사용한다.
샘플 코드는 connector의 public API만 사용해야 하며, 수신 frame을 기록하기 위해
framework 내부 session, actor, dispatch 구현을 직접 건드리지 않는다.

### 공통 샘플 규칙

- observer는 각 sample client가 stream connector를 만든 직후, `connect` 전에 등록한다.
- logging은 sample client 쪽에 둔다. 서버 handler 내부 로그와 섞지 않도록
  `stream-inbound` 같은 고정 marker를 포함한다.
- 한 줄 로그에는 sample 이름, client 역할, message kind, packet name, request sequence,
  payload byte length를 포함한다.
- heartbeat control frame은 observer 기능 검증에는 포함하되, 샘플 기본 출력에서는
  너무 많아지지 않도록 `Control` kind를 낮은 log level로 두거나 명시적으로 걸러낸다.
- observer callback에서는 connector send/request/wait를 다시 호출하지 않는다.
- 샘플 self-check는 적어도 하나의 request 응답과 하나의 push/send 수신 로그가 남는지
  확인한다.

권장 로그 형식:

```text
stream-inbound sample=Bingo client=player1 kind=Response name=authenticate seq=1 bytes=128
stream-inbound sample=TicTacToe client=host kind=Send name=game.state seq=- bytes=256
```

### .NET 샘플

대상:

- `framework/languages/dotnet/samples/Bingo`
- `framework/languages/dotnet/samples/TicTacToe`

계획:

- client connector 생성 helper에 `ObserveInbound(...)` 등록을 추가한다.
- Bingo는 player client별로 auth response, match/start 알림, bound push 수신을 기록한다.
- TicTacToe는 host/guest client별로 join response, game state push, final state push를
  기록한다.
- `run_sample.sh`와 `run_sample.ps1`의 기존 log 검증에 `stream-inbound` marker 검증을
  추가한다.
- 샘플 README에는 observer가 handler 대체 기능이 아니라 수신 관찰용이라는 설명과
  출력 예시를 추가한다.

### Java 샘플

대상:

- `framework/languages/java/samples/java/Bingo`
- `framework/languages/java/samples/java/TicTacToe`

계획:

- Java client connector 생성 지점에서 `observeInbound(...)`를 등록한다.
- `AutoCloseable` registration은 client scenario 수명과 함께 닫는다.
- Bingo와 TicTacToe 모두 request 응답과 push/send 수신 로그를 남긴다.
- Java sample runner 또는 sample test에서 `stream-inbound` marker를 확인한다.
- Java README 또는 sample guide에 DI 없이 connector instance에 observer를 붙이는 예를
  추가한다.

### Kotlin 샘플

대상:

- `framework/languages/java/samples/kotlin/Bingo`
- `framework/languages/java/samples/kotlin/TicTacToe`

계획:

- Kotlin은 별도 connector 구현 대상이 아니라 Java connector의 sample language다.
- Kotlin 샘플은 Java connector API를 사용하므로 Java와 같은 observer 계약을 사용한다.
- Kotlin client connector 생성 helper에 observer 등록을 추가한다.
- coroutine code 안에서도 observer callback은 관찰만 수행하고 connector 호출을 다시
  시작하지 않는다.
- Kotlin sample runner 또는 sample test에서 `stream-inbound` marker를 확인한다.

### Node 샘플

대상:

- `framework/languages/node/samples/Bingo.Ts`
- `framework/languages/node/samples/TicTacToe.Ts`

계획:

- client connector 생성 helper에 `observeInbound(...)` 등록을 추가한다.
- TypeScript sample source와 generated `dist` output이 함께 갱신되도록 기존 Node
  sample build 흐름을 따른다.
- Bingo는 player별 auth/match/start/push 수신 로그를 남긴다.
- TicTacToe는 host/guest별 join response와 game state push 수신 로그를 남긴다.
- `npm run verify:samples` 또는 sample regression test에서 `stream-inbound` marker를
  확인한다.

### C++ 샘플

대상:

- `framework/languages/cpp/samples/Bingo`
- `framework/languages/cpp/samples/TicTacToe`

계획:

- client connector 생성 helper에 `observe_inbound(...)` 등록을 추가한다.
- registration 객체는 client scenario 객체나 connection wrapper가 보관해서 connect 이후
  scenario가 끝날 때까지 살아 있게 한다.
- Bingo는 Protobuf payload를 해석하지 않고 observation의 packet name, kind, byte length만
  기록한다.
- TicTacToe도 JSON payload를 observer에서 parse하지 않는다. 게임 검증은 기존 client
  flow가 담당하고 observer는 공통 수신 로그만 남긴다.
- `run_sample.sh`, `run_sample.ps1`, CTest sample smoke가 `stream-inbound` marker를
  확인하도록 갱신한다.

## 구현 착수 리뷰

아래 항목을 기준으로 다시 검토한 결과, 이 문서 기준으로 구현을 시작해도 되는 상태다.

착수 가능 근거:

- 기능 이름과 역할이 observer로 고정되어 있어 filter처럼 메시지 흐름을 막거나 바꾸는
  계약과 섞이지 않는다.
- observer 등록 방식은 모든 언어에서 생성된 connector instance에 붙이는 방식으로 맞췄다.
- 첫 구현은 `connect` 전 등록만 허용하므로, 연결 중 등록 race와 "어떤 frame부터 보이는가"
  문제를 피한다.
- receive pump에서는 snapshot 생성과 notification enqueue만 수행하고, observer callback은
  별도 queue에서 실행한다.
- request 응답은 pending request 완료 전에 observation을 만들기 때문에 handler로 가지 않는
  response도 관찰할 수 있다.
- queue overflow, observer 실패, metadata snapshot, payload preview, control frame 정책이
  공통 계약에 들어 있다.
- Bingo와 TicTacToe 공통 sample spec에도 observer logging release gate를 추가했다.

구현자가 시작 전에 확인할 항목:

- 각 언어의 existing error enum 또는 error kind에 `observer-failed`, `observer-dropped`
  의미를 추가할 위치를 먼저 정한다.
- queue 크기 option의 언어별 이름과 default 1024 적용 위치를 먼저 정한다.
- observer dispatch queue가 connector close/dispose 때 남은 callback을 어떻게 정리하는지
  언어별 lifecycle 테스트에 포함한다.
- Node는 observer promise rejection을 반드시 connector error path로 연결하고, unhandled
  rejection으로 남기지 않는다.
- C++는 observer callback 예외가 receive lock 밖에서 잡히고 `on_error`로 보고되는지 먼저
  테스트로 고정한다.

남은 설계 이슈:

- 현재 문서 기준으로 구현을 막는 설계 이슈는 없다.
- 연결 중 observer 추가/삭제, payload 본문 전체 관찰, 메시지 drop/변환 기능은 이번 구현
  범위 밖이며 별도 draft가 필요하다.

## 구현 순서

아래 순서는 goal 실행용 작업 순서다. 각 단계는 구현, 테스트, 문서, 누락 검토를 함께
완료해야 다음 단계로 넘어갈 수 있다. 중간에 실패하거나 누락이 확인되면 같은 단계를
수정한 뒤 다시 검토한다. 마지막 단계의 Codex 에이전트 리뷰에서 누락 항목이 없다는
결과가 나와야 goal을 완료 처리할 수 있다.

### 1단계. 공통 계약 고정

작업:

- observation 필드 이름과 payload preview 정책을 각 언어 public type으로 옮긴다.
- observer 실패와 queue overflow error code를 추가한다.
- observer queue 크기 option과 default 1024를 추가한다.
- connect 전 등록, 연결 후 등록 실패, registration 해제 후 미호출 계약을 public API
  문서와 테스트 이름에 반영한다.

검증:

- 네 언어 계획표를 다시 보고 공통 필드와 error 의미가 빠진 언어가 없는지 확인한다.
- `observer-failed`, `observer-dropped`, queue default 1024, connect 전 등록 제한이
  각 언어 구현 계획과 테스트 계획에 모두 존재하는지 확인한다.

다음 단계로 넘어가기 전 누락 검토:

- observation 필드, error code, queue option, registration lifecycle 중 하나라도
  언어별 계획에 없으면 다음 단계로 넘어가지 않는다.

### 2단계. .NET 기준 구현

작업:

- `.NET` connector public API에 `ObserveInbound(...)`와 observation type을 추가한다.
- receive dispatcher에서 pending request 완료보다 먼저 observation을 만든다.
- observer callback은 receive path에서 직접 실행하지 않고 bounded queue로 넘긴다.
- observer queue overflow, observer failure, registration lifecycle을 구현한다.

검증:

- response 관찰, send 관찰, control 관찰, observer failure, overflow, metadata snapshot,
  registration dispose, 연결 후 등록 실패 테스트를 추가한다.
- 느린 observer가 request completion을 지연시키지 않는지 검증한다.
- `.NET` connector 문서와 guide 예제를 갱신한다.

다음 단계로 넘어가기 전 누락 검토:

- 위 테스트 중 하나라도 없거나 실패하면 Node 구현으로 넘어가지 않는다.
- `.NET` 구현에서 생긴 계약 수정 사항이 있으면 이 draft와 공통 계약을 먼저 갱신한다.

### 3단계. Node 구현

작업:

- Node connector interface에 `observeInbound(...)`와 observation type을 추가한다.
- `dispatchFrame(...)`에서 pending request resolve/reject보다 먼저 observation을 만든다.
- observer promise를 `await`하지 않고 별도 queue 또는 promise chain으로 실행한다.
- promise rejection은 connector error path로 연결하고 unhandled rejection으로 남기지 않는다.

검증:

- response 관찰, send 관찰, control 관찰, promise reject, overflow, metadata snapshot,
  returned disposable dispose, 연결 후 등록 실패 테스트를 추가한다.
- 느린 observer가 request promise completion을 지연시키지 않는지 검증한다.
- Node connector 문서와 package README 또는 spec 예제를 갱신한다.

다음 단계로 넘어가기 전 누락 검토:

- TypeScript source와 generated `dist` output이 필요한 경우 함께 갱신되었는지 확인한다.
- `npm run verify:samples` 전 단계의 connector test가 실패하면 Java 구현으로 넘어가지 않는다.

### 4단계. Java 구현

작업:

- Java connector public API에 `observeInbound(...)`와 observation type을 추가한다.
- `ZLinkStreamConnectorOptions` record 생성자는 늘리지 않는다.
- `DefaultZLinkStreamConnector.dispatchInbound(...)`에서 pending request completion보다 먼저
  observation을 만든다.
- observer 전용 bounded executor 또는 queue를 구현한다.

검증:

- response, error response, send, request, control 관찰 테스트를 추가한다.
- observer exception, overflow, metadata snapshot, `AutoCloseable` close, 연결 후 등록 실패,
  느린 observer 테스트를 추가한다.
- Java connector spec과 README 또는 guide 예제를 갱신한다.

다음 단계로 넘어가기 전 누락 검토:

- Java 구현이 Kotlin 샘플에서 그대로 사용할 수 있는 public API인지 확인한다.
- Java connector test가 실패하면 C++ 구현으로 넘어가지 않는다.

### 5단계. C++ 구현

작업:

- C++ connector public API에 `observe_inbound(...)`, `inbound_observation_t`,
  `inbound_observer_t`를 추가한다.
- receive lock 안에서는 observation 생성과 enqueue만 수행한다.
- response frame이 `completed_requests`로 이동하기 전에 observer notification을 등록한다.
- registration 객체가 scenario 수명 동안 observer를 유지하게 한다.

검증:

- response frame 관찰과 pending request callback 완료 순서 테스트를 추가한다.
- manual/immediate dispatch 모드별 observer 실행 경계를 검증한다.
- observer callback 예외, overflow, metadata snapshot, registration 해제, 연결 후 등록 실패,
  느린 observer 테스트를 추가한다.
- C++ connector guide와 contract/header coverage test를 갱신한다.

다음 단계로 넘어가기 전 누락 검토:

- receive lock 안에서 observer callback이 실행되는 경로가 남아 있으면 문서/샘플 단계로
  넘어가지 않는다.
- 새 public header가 있으면 contract-header coverage에 포함되었는지 확인한다.

### 6단계. 정식 문서 반영

작업:

- 각 언어 guide에는 사용 예제를 둔다.
- 정식 spec에는 observer가 메시지를 막거나 바꾸지 않는다는 계약을 명시한다.
- draft 문서는 구현과 테스트가 끝난 뒤 정식 문서로 나누어 반영한다.
- `doc/spec/draft/connector/README.ko.md` 링크 상태를 갱신한다.

검증:

- 정식 spec, guide, package README 사이에 등록 방식이 options 방식으로 되돌아간 문구가
  없는지 검색한다.
- `observer`, `observeInbound`, `observe_inbound`, `stream-inbound` 관련 링크가 깨지지
  않는지 확인한다.

다음 단계로 넘어가기 전 누락 검토:

- 정식 문서와 구현 API 이름이 다르면 샘플 반영으로 넘어가지 않는다.

### 7단계. 샘플 반영

작업:

- Bingo와 TicTacToe client connector 생성 지점에 observer logging을 추가한다.
- .NET, Java, Kotlin, Node, C++ 샘플 runner에서 `stream-inbound` marker를 검증한다.
- Kotlin은 Java connector 구현 완료 뒤 같은 API를 사용하는 샘플 반영 범위로 처리한다.
- 샘플 README 또는 guide에 출력 예시를 추가한다.

검증:

- `framework/doc/spec/sample/bingo/README.ko.md`와
  `framework/doc/spec/sample/tictactoe/README.ko.md`의 release gate를 다시 확인한다.
- 각 언어의 Bingo와 TicTacToe 샘플에서 request 응답 로그와 push/send 수신 로그가 모두
  남는지 확인한다.
- heartbeat control frame 로그가 기본 sample output을 과도하게 채우지 않는지 확인한다.

다음 단계로 넘어가기 전 누락 검토:

- 한 언어라도 Bingo 또는 TicTacToe 중 하나가 빠지면 완료로 보지 않는다.
- sample runner나 regression test가 `stream-inbound` marker를 확인하지 않으면 완료로 보지
  않는다.

### 8단계. 최종 누락 감사

작업:

- 네 connector 구현과 다섯 sample language 반영 상태를 표로 정리한다.
- 각 언어별 test command와 sample runner 결과를 함께 기록한다.
- 실패했거나 실행하지 못한 항목은 완료 항목과 분리해서 남긴다.
- Codex 에이전트 리뷰에 넘길 구현 요약, 변경 파일 목록, 검증 명령 결과, 미실행 사유를
  한 곳에 정리한다.

검증:

- 아래 완료 기준의 모든 bullet을 하나씩 대조한다.
- `rg`로 `observeInbound`, `observe_inbound`, `stream-inbound`,
  `observer-failed`, `observer-dropped`가 필요한 언어와 문서에 모두 반영되었는지 확인한다.
- Codex 에이전트 리뷰가 확인해야 할 범위에 connector 구현, connector 문서, sample 구현,
  sample 문서, runner 검증이 모두 들어 있는지 확인한다.

완료 판정:

- 최종 누락 감사에서 빠진 항목이 0개여야 다음 단계로 넘어갈 수 있다.
- 누락 항목이 하나라도 있으면 해당 구현, 문서, 테스트, sample 단계로 돌아가 수정한다.

### 9단계. Codex 에이전트 반복 리뷰

작업:

- 별도 Codex 에이전트에게 구현 결과를 리뷰하게 한다.
- 리뷰 요청에는 이 draft 문서, 구현 변경 파일, 테스트 파일, sample runner 변경, sample
  README 변경, 실행한 검증 명령과 결과를 모두 포함한다.
- 리뷰 범위는 기능 동작, public API 일관성, 언어별 사용법 일치, 문서 반영, sample logging,
  누락 테스트, 완료 기준 충족 여부를 포함한다.

검증:

- Codex 에이전트 리뷰 결과에 누락, 계약 불일치, 테스트 공백, 문서 불일치, sample 누락이
  하나라도 나오면 완료하지 않는다.
- 나온 지적은 해당 단계로 돌아가 수정하고, 같은 범위로 다시 Codex 에이전트 리뷰를 실행한다.
- 리뷰와 수정은 "누락된 내용이 없다"는 리뷰 결과가 나올 때까지 반복한다.

완료 판정:

- 최종 Codex 에이전트 리뷰 결과가 누락 항목 0개라고 명시해야 goal 완료로 본다.
- 리뷰 결과, 수정 내역, 재검증 명령 결과를 함께 남기지 않으면 완료로 보지 않는다.

## 완료 기준

- C++, Java, .NET, Node connector에 observer 등록 API가 있다.
- request 응답, send, request, error, control 수신을 관찰할 수 있다.
- observer는 connector의 request completion과 handler dispatch를 지연시키지 않는다.
- observer 실패가 원래 메시지 처리를 막지 않는다.
- observer queue overflow가 원래 메시지 처리를 막지 않고 drop error로 관찰된다.
- observer registration은 connect 전 등록, 해제 후 미호출 계약을 지킨다.
- metadata와 payload preview는 observer가 connector 내부 상태를 바꿀 수 없도록
  제공된다.
- 각 언어별 테스트가 response 관찰과 느린 observer를 포함한다.
- 각 언어 guide 또는 spec 문서에 사용 예제와 제한 사항이 반영된다.
- Bingo와 TicTacToe 샘플은 .NET, Java, Kotlin, Node, C++에서 observer 기반
  `stream-inbound` 로그를 출력한다.
- 샘플 runner 또는 regression test는 request 응답과 push/send 수신 로그 marker를
  확인한다.
- Codex 에이전트 반복 리뷰에서 누락된 내용이 없다는 결과가 나왔다.
