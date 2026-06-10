# Java/Kotlin Stream Dispatch And Sample Cleanup Implementation Plan

이 문서는 Java/Kotlin sample, framework, Java bindings 라이브러리에 걸친 구현 실행
문서다. 작업자는 이 문서를 보고 수정 대상, public interface 모양, 테스트 항목, 문서 반영
순서를 따라 작업한다.

아직 구현이 끝난 공개 계약은 아니므로 정식 spec 문서에 바로 섞지 않는다. 구현과 검증이
끝난 뒤 실제 public API와 맞는 내용만 기존 spec, guide, internals 문서에 나누어 반영한다.

## 1. 배경

현재 논의의 핵심은 sample code가 framework 사용법을 보여 주지 못하고, Java/Kotlin
connector API가 `.NET` sample보다 장황하게 보이며, native C API 대기 지점과 Java virtual
thread 또는 Kotlin coroutine 실행 모델의 경계가 문서와 코드에서 충분히 닫혀 있지 않다는
점이다.

`.NET` sample은 connector 자체 API로 request, wait, response 검증이 순서대로 읽힌다.
Java/Kotlin도 같은 수준으로 맞춘다. codec이 JSON, MessagePack, Protobuf로 바뀌어도 sample
scenario의 호출 모양은 바뀌지 않아야 한다. sample-local helper, inbox, queue, `submitAsync`
같은 이름이 client scenario의 중심이 되면 사용자가 실제 framework API를 읽기 어렵다.

동시에 Java bindings는 native C API를 호출한다. native blocking `recv`를 virtual thread마다
직접 호출하는 구조는 확장 가능한 실행 모델로 보장하기 어렵다. bindings는 low-level blocking
API를 유지할 수 있지만, framework가 사용할 scalable path는 poller 또는 non-blocking recv를
기준으로 한 dispatch boundary를 별도로 가져야 한다.

## 2. 목표

이 계획의 목표는 아래와 같다.

- TicTacToe와 Bingo Java/Kotlin sample을 공통 sample spec과 `.NET` 기준에 맞춘다.
- client scenario는 connector public API만으로 읽히게 한다.
- Java connector는 `submit`과 `await` 의미를 분리한다.
- 별도 timeout을 지정하지 않은 `waitFor`는 connector options의 `requestTimeout`을 따른다.
- predicate가 필요한 `waitFor`는 fluent builder의 `where`로 표현한다.
- Java framework handler는 virtual thread executor에서 실행할 수 있게 한다.
- Kotlin framework handler는 coroutine dispatcher와 scope를 기준으로 실행할 수 있게 한다.
- bindings/java는 native blocking wait와 framework dispatch boundary의 책임을 분리한다.
- 기존 문서는 구현 전 계획, 공개 API 계약, 사용자 guide, 내부 구조 설명을 섞지 않게 정리한다.

## 3. 비목표

- low-level bindings API에서 blocking `recv(RecvFlags.NONE)`를 즉시 제거하지 않는다.
- native blocking `recv`를 client 또는 handler 수만큼 virtual thread에 직접 태우는 구조를
  권장하지 않는다.
- Kotlin handler를 Java virtual thread handler model에 강제로 맞추지 않는다.
- sample 흐름을 숨기는 공통 helper tree를 새로 만들지 않는다.
- 구현되지 않은 API를 정식 spec 문서에 완료된 계약처럼 적지 않는다.
- TicTacToe의 SessionGateway, gateway 변형, reconnect 변형을 유지하지 않는다. 공통 spec
  기준의 기본 TicTacToe sample만 유지한다.

## 4. 실행 모델 결정

### 4.1 Java connector 호출 모델

Java connector의 call builder는 아래 의미를 가져야 한다.

- `submit`은 공식 비동기 API다. 작업을 시작하고 `CompletionStage`를 반환한다.
- `await`는 Java 전용 blocking convenience다. 내부적으로 `submit`을 호출하거나 이미 받은
  `CompletionStage`의 완료를 현재 Java thread에서 기다린다.
- connect, disconnect, reconnect, close, dispatch, send, request, wait 모두 같은 call builder
  규칙을 따른다.
- Java에서 `await`는 thread 대기다. virtual thread에서 호출하면 carrier 사용 비용을 줄일 수
  있지만, 코드 모양은 platform thread와 같아야 한다. Java sample은 virtual thread 또는 sample
  main thread에서 시나리오를 읽기 쉽게 표현하기 위해 `await`를 사용할 수 있다.
- Kotlin에서 `await`는 coroutine suspension이다. 이름은 같지만 실행 의미는 Kotlin wrapper가
  책임진다. Kotlin 사용자 code와 Kotlin sample은 Java `submit()`을 직접 호출하지 않고 Kotlin
  wrapper가 제공하는 `await()`만 사용한다.
- Kotlin wrapper의 `await()`는 Java blocking `await()`를 호출하지 않는다. Java call builder의
  `submit()`이 반환한 `CompletionStage`를 `kotlinx-coroutines-jdk8`의 `await()`로 기다린다.

Java sample은 아래처럼 읽히는 것을 목표로 한다.

```java
client1.connect().await();

var client1Auth = client1.request(new AuthenticateReq("player-1")).await(AuthenticateRes.class);
Ensure(client1Auth.actorId().equals("player-1"));

var client1SawClient2Join = client1
    .waitFor(SampleNames.PlayerJoinedPacket)
    .where(message -> message.payload().actorId().equals("player-2"))
    .submit(Messages.PlayerJoinedNotify.class);
```

Kotlin sample은 Kotlin 전용 wrapper로 타입 인자를 반복하지 않는 모양을 우선한다.

```kotlin
client1.connect().await()

val client1Auth = client1.request(AuthenticateReq("player-1")).await<AuthenticateRes>()
ensure(client1Auth.actorId == "player-1")

val client1SawClient2Join = client1
    .waitFor<PlayerJoinedNotify>(SampleNames.PlayerJoinedPacket)
    .where { it.payload().actorId == "player-2" }
    .await()
```

### 4.2 bindings/java native wait boundary

bindings/java는 C API를 감싼다. 따라서 low-level API가 native blocking 동작을 그대로 노출하는
것 자체는 문제가 아니다. 문제는 framework가 그 API를 확장 가능한 handler 실행 경로로 직접
쓰는 경우다.

bindings/java에는 아래 경계를 마련한다.

- native wait는 적은 수의 runtime thread에서 수행한다.
- 가능하면 poller wait와 `DONT_WAIT` recv 조합으로 ready handle을 수집한다.
- native wait 결과는 Java queue, callback, `CompletionStage` 같은 Java-side dispatch
  primitive로 넘긴다.
- framework handler 실행은 이 dispatch boundary 뒤에서 수행한다.
- blocking recv API에는 low-level/native-blocking 성격과 virtual thread 대량 사용 시 주의점을
  문서화한다.

이 경계는 bindings 라이브러리 차원에서 제공하는 것이 바람직하다. framework마다 native wait
loop를 다시 만들면 같은 위험한 지식이 여러 곳으로 퍼진다.

### 4.3 Java framework handler execution

Java framework는 dispatch boundary에서 넘어온 message를 handler executor에 전달한다.
기본 handler executor는 virtual thread executor를 사용할 수 있게 한다. 다만 native recv
대기는 handler virtual thread에서 직접 수행하지 않는다.

구성값에는 아래가 필요하다.

- handler executor 선택
- virtual thread 사용 여부
- handler concurrency limit
- graceful shutdown timeout
- handler 실패 시 stream/session/actor lifecycle 처리 규칙

### 4.4 Kotlin framework handler execution

Kotlin framework는 Kotlin 전용 coroutine handler dispatcher를 제공한다. Java virtual thread
executor를 그대로 기본값으로 쓰지 않는다.

Kotlin 쪽 원칙은 아래와 같다.

- suspend handler는 `CoroutineDispatcher`와 `CoroutineScope`에서 실행한다.
- Java dispatch boundary는 재사용하되, Kotlin wrapper는 `Flow`, `Channel`, suspend API로 감싼다.
- blocking Java workload를 감싸야 할 때만 virtual-thread-backed coroutine dispatcher를 선택할
  수 있게 한다.
- cancellation은 coroutine cancellation으로 표현하고, native handle 정리와 framework lifecycle
  종료는 하위 runtime이 책임진다.

### 4.5 Goal 기반 실행 관리

이 계획은 한 번에 모든 파일을 고치는 작업이 아니다. 작업자는 아래 goal 단위로 나누어 진행하고,
각 goal의 완료 조건을 모두 만족한 뒤 다음 goal로 넘어간다. 한 goal을 진행하는 동안 다른 goal의
파일을 우연히 고쳤다면 그 변경이 왜 필요한지 goal 기록과 commit message에 남긴다.

goal을 만들 때 objective에는 아래 네 가지를 반드시 적는다.

- 수정 대상 범위
- public interface 변경 여부
- 문서 반영 범위
- 실행할 회귀 테스트

goal objective 예시는 아래와 같다.

```text
Java stream connector lifecycle API를 submit/await call builder로 변경한다.
대상은 zlink-stream-connector public API, Kotlin wrapper 연동 지점, Java/Kotlin sample
client 호출부, stream connector spec이다. 검증은 connector unit test, Kotlin wrapper compile,
Java/Kotlin sample client compile, sample release gate로 한다.
```

goal 진행 규칙:

- 같은 goal 안에서 API, sample, test, 문서 중 하나만 고치고 멈추지 않는다.
- public API를 바꾸면 같은 goal 안에서 compile error가 나는 호출부와 spec 문서를 함께 고친다.
- sample style을 바꾸면 같은 goal 안에서 release gate 또는 source scan 항목을 함께 고친다.
- Kotlin wrapper를 바꾸면 같은 goal 안에서 Kotlin sample compile과 coroutine wrapper test를 함께
  확인한다.
- bindings/java native wait boundary를 바꾸면 같은 goal 안에서 framework handler execution 경로가
  native blocking recv를 직접 호출하지 않는지 확인한다.
- goal이 끝났다고 표시하기 전에는 `git diff --check`, 관련 compile/test, 문서 검색 결과를 남긴다.

goal 완료 증거:

| Goal | 필수 증거 |
|------|-----------|
| Stream connector API | public interface diff, old API 검색 결과, connector unit test |
| Kotlin wrapper | wrapper API diff, Kotlin sample source scan, Kotlin compile/test |
| Java sample cleanup | Java client scenario diff, sample release gate, Java sample compile |
| Kotlin sample cleanup | Kotlin client scenario diff, 직접 `submit()` 검색 결과, Kotlin sample compile |
| bindings dispatcher | dispatcher interface diff, native wait boundary test, blocking recv guide update |
| Java handler execution | executor option diff, virtual thread handler test, shutdown/failure test |
| Kotlin handler execution | coroutine dispatcher diff, cancellation test, scope ownership test |
| 문서 반영 | spec/guide/internals/draft diff, 구현 전 API가 정식 spec에 없는지 검색 |

한 goal의 필수 증거 중 하나라도 없으면 다음 goal로 넘어가지 않는다. 시간이 부족해서 일부만
끝났다면 goal을 완료로 표시하지 않고 남은 항목을 known issue로 남긴다.

## 5. 단계별 작업 계획

### Phase 1. 기준 문서와 release gate 정리

1. `framework/doc/spec/sample/bingo/README.ko.md`와
   `framework/doc/spec/sample/tictactoe/README.ko.md`를 다시 읽고 Java/Kotlin sample이 따라야
   하는 디렉토리 구조, DDD/hexagonal 책임, client scenario 기준을 확정한다.
2. Java/Kotlin sample release gate가 sample-local helper, `submitAsync`, predicate-only
   `waitForAsync`, 불필요한 result 객체를 잡아낼 수 있게 검사 항목을 추가한다.
3. Java/Kotlin 문서의 현재 draft와 spec이 충돌하는 부분을 목록화한다. 구현 전 결정은 draft나
   이 plan에 남기고, 이미 구현된 public API만 spec에 둔다.

완료 기준:

- sample spec과 Java/Kotlin sample 구현 기준의 충돌 목록이 없다.
- release gate가 금지된 sample style을 탐지한다.

작업 파일:

- `framework/doc/spec/sample/bingo/README.ko.md`
- `framework/doc/spec/sample/tictactoe/README.ko.md`
- `framework/languages/java/src/test/.../SampleReleaseGateContractTest.java`
- `framework/languages/java/samples/java/Bingo/Client/.../BingoClientApp.java`
- `framework/languages/java/samples/java/TicTacToe/Client/.../TicTacToeClient.java`
- `framework/languages/java/samples/kotlin/Bingo/Client/.../BingoClient.kt`
- `framework/languages/java/samples/kotlin/TicTacToe/Client/.../TicTacToeClient.kt`

release gate에 추가할 검사:

- sample client source에 `submitAsync(`가 있으면 실패한다.
- sample client source에 `waitForAsync(`가 있으면 실패한다.
- sample client source에 `SampleAsync.`가 있으면 실패한다.
- TicTacToe sample 경로 또는 문서에 `SessionGateway`가 있으면 실패한다.
- client scenario에서 business request를 감싸는 sample-local helper 이름이 발견되면 실패한다.
  허용되는 helper는 `Ensure`, `ensure`, process 실행, endpoint 구성처럼 scenario 외곽 준비에
  한정한다.

### Phase 2. Sample client scenario 정리

1. Java Bingo client에서 `SampleAsync.await(...)` 같은 helper를 제거하고 connector의
   `await`, `submit`만 사용한다.
2. Java TicTacToe client를 Bingo client와 같은 스타일로 맞춘다.
3. Kotlin Bingo와 TicTacToe client는 Kotlin wrapper의 `request(...).await<T>()`,
   `waitFor<T>(...).where { ... }.await()` 형태로 정리한다.
4. response 검증은 별도 result 객체가 아니라 `Ensure` 또는 `ensure`로 request 직후 수행한다.
5. push 검증은 sample-local queue나 handler 등록 코드가 아니라 connector `waitFor` builder로
   표현한다.
6. 기본 timeout은 client scenario에 쓰지 않는다. 기본값과 다른 경우만 명시한다.

완료 기준:

- `submitAsync`가 sample client scenario에 남아 있지 않다.
- `SampleAsync`, inbox queue, helper 함수가 business flow를 숨기지 않는다.
- TicTacToe는 공통 spec 기준 기본 sample만 남는다.
- Kotlin sample client scenario에는 직접 `submit()` 호출이 남아 있지 않다.
- Java/Kotlin sample compile과 sample release gate가 통과한다.

Java sample code style:

```java
var client1Auth = client1.request(new AuthenticateReq("player-1")).await(AuthenticateRes.class);
Ensure(client1Auth.actorId().equals("player-1"));

var client1SawClient2Join = client1
    .waitFor(SampleNames.PlayerJoinedPacket)
    .where(message -> message.payload().actorId().equals("player-2"))
    .submit(Messages.PlayerJoinedNotify.class);

var client2Join = client2.request(new JoinRoomReq(roomId)).await(JoinRoomRes.class);
Ensure(client2Join.accepted());

var client1JoinNotify = client1.await(client1SawClient2Join);
Ensure(client1JoinNotify.payload().actorId().equals("player-2"));
```

Kotlin sample code style:

```kotlin
val client1Auth = client1.request(AuthenticateReq("player-1")).await<AuthenticateRes>()
ensure(client1Auth.actorId == "player-1")

val client1SawClient2Join = client1
    .waitFor<PlayerJoinedNotify>(SampleNames.PlayerJoinedPacket)
    .where { it.payload().actorId == "player-2" }
    .await()

val client2Join = client2.request(JoinRoomReq(roomId)).await<JoinRoomRes>()
ensure(client2Join.accepted)

ensure(client1SawClient2Join.payload().actorId == "player-2")
```

sample에서 금지할 코드:

```java
CompletionStage<ZLinkStreamMessage<PlayerJoinedNotify>> stage =
    client1.waitForAsync(SampleNames.PlayerJoinedPacket, PlayerJoinedNotify.class);

var auth = SampleAsync.await(client1.request(new AuthenticateReq("player-1")).submitAsync(...));
```

### Phase 3. Stream Connector public API 정리

1. Java stream connector spec과 구현에서 `submit`, `await`, `where`의 의미를 고정한다.
2. predicate-only `waitForAsync` public overload는 제거하거나 내부 구현 세부사항으로 내린다.
3. `waitFor` timeout 기본값은 connector options의 `requestTimeout`으로 통일한다.
4. codec별 request/wait 호출 모양이 달라지지 않게 typed connector API와 codec module API를
   다시 검토한다.
5. Kotlin wrapper는 Java API를 단순히 노출하는 수준을 넘어서 Kotlin다운 typed `await<T>()`와
   suspend 대기를 제공한다. Kotlin 사용자 code에는 Java `submit()`을 직접 노출하지 않는다.

완료 기준:

- JSON, MessagePack, Protobuf codec에서 같은 connector call shape를 사용한다.
- Java sample은 필요한 경우에만 `Class<T>`를 전달한다.
- Java `submit()`은 공식 async API이고, Java `await()`는 virtual thread 친화적인 blocking
  adapter라는 점이 code와 문서에서 분리된다.
- Kotlin sample은 반복적인 `SomeClass::class.java` 인자가 client scenario를 지배하지 않는다.
- Kotlin sample은 `submit()`을 직접 호출하지 않고 Kotlin wrapper의 `await()`만 사용한다.

구현할 Java public interface 모양:

```java
public interface ZLinkStreamConnector {
    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall disconnect();
    ZLinkStreamLifecycleCall reconnect();
    ZLinkStreamLifecycleCall close();
    ZLinkStreamLifecycleCall dispatch();

    <T> T await(CompletionStage<T> stage) throws Exception;

    ZLinkStreamSendCall send(Object payload);
    ZLinkStreamTypedRequestCall request(Object payload);
    ZLinkStreamWaitCall waitFor(String name);
    ZLinkStreamWaitCall waitFor(Class<?> payloadType);
}

public interface ZLinkStreamLifecycleCall {
    CompletionStage<Void> submit();
    void await() throws Exception;
}

public interface ZLinkStreamSendCall {
    CompletionStage<Void> submit();
    void await() throws Exception;
}

public interface ZLinkStreamTypedRequestCall {
    CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit();
    <T> CompletionStage<ZLinkStreamMessage<T>> submit(Class<T> payloadType);
    <T> T await(Class<T> payloadType) throws Exception;
}

public interface ZLinkStreamWaitCall {
    ZLinkStreamWaitCall timeout(Duration timeout);
    ZLinkStreamWaitCall where(Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate);
    <T> ZLinkStreamWaitCall where(
        Class<T> payloadType,
        Predicate<ZLinkStreamMessage<T>> predicate);
    CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit();
    <T> CompletionStage<ZLinkStreamMessage<T>> submit(Class<T> payloadType);
    <T> ZLinkStreamMessage<T> await(Class<T> payloadType) throws Exception;
}
```

호환성 없이 진행하므로 아래 old API는 제거한다.

- `connectAsync`
- `disconnectAsync`
- `reconnectAsync`
- `closeAsync`
- `dispatchAsync`
- `submitAsync`
- `awaitConnect`
- `awaitDisconnect`
- `awaitReconnect`
- `awaitClose`
- `awaitDispatch`
- public predicate-only `waitForAsync`

구현 규칙:

- 모든 network operation의 기준 API는 `submit()`이다. `submit()`은 `CompletionStage`를 반환한다.
- Java `await()`는 async API 위에 얹은 blocking adapter다. 별도 network 의미를 만들지 않고
  `submit()` 결과를 기다리는 역할만 한다.
- Java `await()`는 platform thread에서도 동작하지만 권장 사용처는 virtual thread다.
- Kotlin wrapper의 `await()`는 coroutine suspension이다. Java blocking `await()`를 호출하지 않고
  Java `submit()`으로 얻은 `CompletionStage`를 coroutine `await()`로 기다린다.
- Kotlin wrapper는 lifecycle, send, request, wait call에 suspend `await()`를 제공한다. Kotlin
  sample과 guide에는 `submit()` 호출을 노출하지 않는다.
- `waitFor(...).submit(...)`에서 timeout을 지정하지 않으면
  `ZLinkStreamConnectorOptions.requestTimeout()`을 사용한다.
- `timeout(Duration)`을 호출한 경우에만 해당 call의 timeout을 덮어쓴다.
- `where(...)`는 message를 소비하기 전에 predicate를 평가한다. predicate가 false면 다음
  matching message를 계속 기다린다.
- `where(...)` 내부 예외는 returned stage를 실패로 완료한다.
- `Class<T>` 기반 decode는 codec module에 위임한다. call builder는 codec 종류를 분기하지 않는다.

구현 파일 후보:

- `framework/languages/java/zlink-stream-connector/src/main/java/.../ZLinkStreamConnector.java`
- `framework/languages/java/zlink-stream-connector/src/main/java/.../ZLinkStreamSendCall.java`
- `framework/languages/java/zlink-stream-connector/src/main/java/.../ZLinkStreamTypedRequestCall.java`
- `framework/languages/java/zlink-stream-connector/src/main/java/.../ZLinkStreamWaitCall.java`
- `framework/languages/java/zlink-framework-kotlin/src/main/kotlin/.../ZLinkConnectorExtensions.kt`

### Phase 4. bindings/java dispatch boundary 추가

1. 현재 bindings/java의 blocking recv, poller, callback, handle ownership API를 점검한다.
2. framework가 사용할 dispatch boundary API 후보를 두 가지 이상 비교한다.
   - 후보 A: bindings가 `ZlinkPollDispatcher` 같은 dispatcher를 제공한다.
   - 후보 B: bindings는 poller와 non-blocking recv helper만 제공하고 framework가 dispatcher를
     구성한다.
3. POSD 기준으로 후보 A를 우선 검토한다. native wait 지식이 framework로 새는 것을 줄일 수
   있기 때문이다.
4. dispatcher는 적은 수의 runtime thread에서 poll/recv를 수행하고, Java-side callback 또는
   stage completion으로 message를 넘긴다.
5. blocking recv API 문서에는 low-level API라는 점과 virtual thread 대량 사용 주의점을 적는다.

완료 기준:

- framework code가 native blocking recv를 handler 실행 thread에서 직접 호출하지 않는다.
- bindings 문서가 low-level blocking API와 framework dispatch path를 구분한다.
- dispatcher 또는 poller boundary에 대한 unit/contract test가 있다.

구현할 bindings API 초안:

```java
public final class ZlinkDispatchOptions {
    public static Builder builder();

    public int runtimeThreadCount();
    public Duration pollTimeout();
    public int maxBatchSize();
    public Executor callbackExecutor();
    public boolean failFastOnClosedHandle();
}

public interface ZlinkNativeDispatcher extends AutoCloseable {
    ZlinkDispatcherRegistration register(
        ZlinkSocket socket,
        ZlinkReceiveHandler handler);

    CompletionStage<Void> start();
    CompletionStage<Void> stop();
    boolean isRunning();
}

public interface ZlinkDispatcherRegistration extends AutoCloseable {
    boolean isClosed();
}

@FunctionalInterface
public interface ZlinkReceiveHandler {
    CompletionStage<Void> handle(ZlinkReceivedMessage message);
}
```

이름은 구현 전에 한 번 더 검토한다. 핵심은 API 이름이 아니라 책임이다. native poll/recv 지식은
bindings dispatcher 안에 있어야 하고, framework handler는 이미 Java object로 올라온 message만
받아야 한다.

dispatcher 내부 동작:

1. runtime thread가 poller wait를 호출한다.
2. ready socket에 대해 `DONT_WAIT` recv를 반복한다.
3. 수신 message를 Java object로 변환한다.
4. `callbackExecutor` 또는 framework adapter callback으로 넘긴다.
5. handle close, peer disconnect, timeout, native error를 명확한 Java exception 또는 terminal
   event로 변환한다.

blocking API 문서화:

- `ZlinkSocket.recv(RecvFlags.NONE)` 같은 API는 low-level native blocking API로 남긴다.
- 해당 API는 적은 수의 전용 thread에서 쓰거나 테스트에서 쓰는 API로 설명한다.
- virtual thread를 많이 만들어 native blocking recv를 직접 호출하는 sample은 작성하지 않는다.

### Phase 5. Java framework handler dispatcher 정리

1. Java framework runtime에 handler executor abstraction을 둔다.
2. 기본값으로 virtual thread executor를 사용할지, 옵션으로만 제공할지 결정한다.
3. handler concurrency limit과 shutdown 의미를 public configuration으로 정리한다.
4. stream/session/actor/spot handler가 같은 dispatch rule을 따르도록 공통 실행 경로를 만든다.
5. native wait boundary와 handler executor boundary를 테스트로 분리한다.

완료 기준:

- Java framework handler는 virtual thread에서 실행할 수 있다.
- native recv 대기는 handler virtual thread에서 직접 일어나지 않는다.
- handler failure, cancellation, shutdown test가 통과한다.

구현할 framework API 초안:

```java
public final class ZLinkHandlerExecutionOptions {
    public static Builder builder();

    public ZLinkHandlerExecutorKind executorKind();
    public int maxConcurrency();
    public Duration shutdownTimeout();
    public Optional<ExecutorService> executorOverride();
}

public enum ZLinkHandlerExecutorKind {
    VIRTUAL_THREAD,
    FIXED_THREAD_POOL,
    DIRECT
}

public interface ZLinkHandlerDispatcher extends AutoCloseable {
    CompletionStage<Void> dispatch(ZLinkHandlerInvocation invocation);
    CompletionStage<Void> shutdown();
}
```

framework adapter 규칙:

- stream, channel, spot, actor/session handler는 같은 `ZLinkHandlerDispatcher`를 통과한다.
- handler method 호출 전 validation과 codec decode는 기존 framework 규칙을 따른다.
- handler method 내부에서 발생한 예외는 해당 protocol의 error response, disconnect, lifecycle
  failure 규칙으로 변환한다.
- dispatcher shutdown은 새 handler 접수를 멈추고 진행 중 handler를 `shutdownTimeout` 안에
  기다린다.
- `DIRECT` executor는 test 전용 또는 명시 옵션으로만 사용한다.

### Phase 6. Kotlin framework coroutine dispatcher 정리

1. Kotlin framework에 coroutine handler dispatcher와 scope 설정을 둔다.
2. Java framework의 virtual thread executor 설정을 그대로 기본값으로 노출하지 않는다.
3. suspend handler, `Flow`, `Channel` 기반 wait/receive API를 Kotlin wrapper에서 제공한다.
4. blocking Java handler를 감싸야 하는 경우에만 virtual-thread-backed coroutine dispatcher를
   선택할 수 있게 한다.
5. cancellation과 shutdown test를 Java framework test와 별도로 둔다.

완료 기준:

- Kotlin handler는 suspend 함수로 자연스럽게 작성된다.
- Kotlin sample은 Java `Class<T>` 반복 없이 request/wait scenario를 표현한다.
- Kotlin coroutine cancellation이 framework lifecycle 정리와 충돌하지 않는다.

구현할 Kotlin API 초안:

```kotlin
class ZLinkCoroutineHandlerOptions(
    val dispatcher: CoroutineDispatcher,
    val scope: CoroutineScope? = null,
    val shutdownTimeout: Duration,
)

interface ZLinkCoroutineHandlerDispatcher : AutoCloseable {
    suspend fun dispatch(invocation: ZLinkCoroutineHandlerInvocation)
    suspend fun shutdown()
}

suspend inline fun <reified T : Any> ZLinkStreamTypedRequestCall.await(): T

suspend fun ZLinkStreamLifecycleCall.await()

suspend fun ZLinkStreamSendCall.await()

inline fun <reified T : Any> ZLinkStreamConnector.waitFor(name: String): ZLinkKotlinWaitCall<T>

interface ZLinkKotlinWaitCall<T : Any> {
    fun timeout(timeout: Duration): ZLinkKotlinWaitCall<T>
    fun where(predicate: (ZLinkStreamMessage<T>) -> Boolean): ZLinkKotlinWaitCall<T>
    suspend fun await(): ZLinkStreamMessage<T>
}
```

Kotlin 구현 규칙:

- Java `CompletionStage`는 Kotlin wrapper 경계에서 coroutine으로 변환한다.
- Kotlin 사용자 code와 Kotlin sample은 `submit()`을 직접 호출하지 않는다. Kotlin wrapper가
  lifecycle, send, request, wait마다 suspend `await()`를 제공하고 내부에서 Java `submit()`을
  호출한다.
- sample code에는 `SomeClass::class.java`가 반복되지 않게 한다.
- coroutine scope를 framework가 소유하는 기본 경로와 사용자가 넘기는 외부 scope 경로를 둘 다
  테스트한다.
- cancellation은 native handle close가 아니라 handler job 취소로 먼저 표현한다. handle 정리는
  framework lifecycle이 수행한다.
- Kotlin wrapper는 Java blocking `await(Class<T>)`를 직접 호출하지 않는다. Java call builder의
  `submit(Class<T>)`로 `CompletionStage`를 얻고, `kotlinx-coroutines-jdk8`의 `await()`로
  coroutine suspension에 연결한다.
- `inline reified` wrapper, `T::class.java` 조회, `CompletionStage.await()` continuation 연결
  비용은 network I/O, native boundary, codec encode/decode, dispatch queue 비용보다 작다.
  따라서 sample과 일반 framework client 경로에서는 Java connector 위에 얇은 Kotlin wrapper를
  얹는 방식을 기본으로 한다.
- 초고빈도 message hot path, `Flow`/`Channel` backpressure, cancellation을 native dispatch
  lifecycle까지 더 깊게 밀어 넣어야 하는 경우에만 Kotlin 전용 구현을 별도 검토한다.

### Phase 7. 문서 반영과 정리

1. 구현 전 결정은 이 plan과 Java/Kotlin draft 문서에 남긴다.
2. 구현이 끝난 public API만 spec 문서로 이동한다.
3. 사용자가 따라 하는 설명은 guide에 둔다.
4. native wait, poller, dispatcher thread model은 internals에 둔다.
5. sample README와 sample spec은 실제 directory와 code style을 다시 반영한다.

완료 기준:

- spec, guide, internals 문서가 서로 같은 내용을 다른 목적에 맞게 설명한다.
- 정식 spec에 구현되지 않은 API가 없다.
- guide에 native socket wiring 같은 내부 구현 설명이 없다.

## 6. 기존 문서 수정 계획

### 6.1 Sample spec

수정 대상:

- `framework/doc/spec/sample/bingo/README.ko.md`
- `framework/doc/spec/sample/tictactoe/README.ko.md`

수정 내용:

- Java/Kotlin sample도 DDD와 hexagonal 구조를 유지해야 한다는 기준을 확인한다.
- client scenario는 `.NET`처럼 connector API 중심으로 읽혀야 한다고 명시한다.
- TicTacToe는 기본 공통 spec sample만 유지하고 SessionGateway/gateway 변형은 유지하지 않는다고
  적는다.
- default timeout은 scenario code에 반복하지 않고 options 기준으로 동작한다고 설명한다.

### 6.2 Java/Kotlin framework spec과 guide

수정 대상:

- `framework/languages/java/doc/spec/stream-connector.ko.md`
- `framework/languages/java/doc/spec/handler-interfaces.ko.md`
- `framework/languages/java/doc/spec/spring-boot-stream.ko.md`
- `framework/languages/java/doc/spec/spring-boot-actor-session.ko.md`
- `framework/languages/java/doc/spec/spring-boot-spot.ko.md`
- `framework/languages/java/doc/guide/08-stream.ko.md`
- `framework/languages/java/doc/guide/samples/stream-samples.ko.md`

수정 내용:

- `submit`과 `await` 의미를 문서화한다. `submit`은 공식 async API이고 `await`는 Java 전용
  blocking adapter라는 점을 분리한다.
- Java의 `await`는 virtual thread 친화적인 thread wait, Kotlin의 `await`는 coroutine
  suspension이라고 구분한다.
- Kotlin wrapper 문서에는 Kotlin `await()`가 Java blocking `await()`를 호출하지 않고
  `submit()`으로 얻은 `CompletionStage`를 coroutine `await()`로 기다린다고 적는다.
- Java guide의 sample style은 `waitFor(...).where(...).submit(...)`와
  `waitFor(...).where(...).await(...)`를 모두 설명하되, `submit`이 기준 async API임을 먼저
  설명한다.
- Kotlin guide의 sample style은 `connect().await()`, `request(...).await<T>()`,
  `waitFor<T>(...).where { ... }.await()`만 노출한다. Kotlin 사용자-facing guide에는
  `submit()`을 직접 쓰는 client scenario를 넣지 않는다.
- predicate-only wait API가 남아 있다면 public guide에서 제거한다.
- handler execution 설정은 Java와 Kotlin을 분리해 설명한다.

### 6.3 Java/Kotlin internals

수정 대상:

- `framework/languages/java/doc/internals/lifecycle-and-failure-semantics.ko.md`
- `framework/languages/java/doc/internals/behavior-matrix.ko.md`
- `framework/languages/java/doc/internals/regression-test-matrix.ko.md`
- `framework/languages/java/doc/internals/dotnet-to-java-surface-mapping.ko.md`

수정 내용:

- native wait boundary와 handler executor boundary를 구분한다.
- Java virtual thread handler와 Kotlin coroutine handler의 실패, 취소, 종료 의미를 정리한다.
- Java `await()`는 blocking adapter이고 Kotlin `await()`는 coroutine suspension이라는 실행
  차이를 behavior matrix에 반영한다.
- Kotlin wrapper가 Java `submit()`을 내부적으로 호출하지만 사용자-facing Kotlin sample에는
  `submit()`을 노출하지 않는다는 규칙을 regression matrix에 반영한다.
- `.NET` 기준과 Java/Kotlin 차이를 표로 정리하되, sample scenario 의미는 같게 유지한다.
- release gate가 확인해야 하는 sample style 항목을 추가한다.

### 6.4 bindings 문서

수정 대상:

- `doc/spec/bindings/java/README.ko.md`
- `doc/guide/bindings/java/index.ko.md`
- `doc/spec/bindings/README.ko.md`
- 필요하면 `doc/internals/threading-model.ko.md`
- 필요하면 `doc/internals/io-thread.ko.md`

수정 내용:

- bindings/java low-level blocking recv API의 성격을 명시한다.
- framework가 사용할 poller/dispatcher boundary를 public API로 추가한 뒤 spec에 반영한다.
- 사용자가 virtual thread를 쓰더라도 native blocking recv를 대량으로 직접 호출하는 구조는
  권장하지 않는다고 guide에 설명한다.
- poller/dispatcher thread model은 internals에 둔다.

### 6.5 Java/Kotlin draft 문서

수정 대상:

- `framework/languages/java/doc/draft/sample-implementation-plan.ko.md`
- `framework/languages/java/doc/draft/stream-connector.ko.md`
- `framework/languages/java/doc/draft/java-kotlin-framework-porting-plan.ko.md`
- `framework/languages/java/doc/draft/implementation-execution-plan.ko.md`

수정 내용:

- 아직 구현되지 않은 dispatcher API, Kotlin coroutine handler API, release gate 보강 항목은 draft에
  남긴다.
- Java async core, Java virtual-thread blocking adapter, Kotlin coroutine wrapper의 역할 분리를
  draft와 정식 문서 사이에서 같은 용어로 유지한다.
- Kotlin sample migration 항목에는 `submit()` 직접 사용 금지와 wrapper `await()` 사용을 포함한다.
- 구현이 완료된 항목은 spec, guide, internals로 옮긴 뒤 draft에서 중복 설명을 줄인다.

## 7. 검증 계획

검증은 아래 순서로 진행한다. Gradle build가 native bindings jar를 동시에 건드릴 수 있으므로,
bindings jar를 만드는 단계와 sample compile/smoke 단계는 필요하면 순차 실행한다.

1. bindings/java unit test와 poller/dispatcher contract test
2. `zlink-stream-connector` unit test
3. `zlink-stream-connector-json`, MessagePack, Protobuf codec contract test
4. Java framework unit test와 integration test
5. Kotlin wrapper compile과 coroutine handler test
6. Java Bingo, Java TicTacToe client/server compile
7. Kotlin Bingo, Kotlin TicTacToe client/server compile
8. Java/Kotlin sample release gate
9. standalone sample smoke
10. `git diff --check`

## 7.1 상세 회귀 테스트 항목

### Stream connector tests

- `request(...).submit(Class<T>)`가 stage를 반환하고 response payload를 decode한다.
- `request(...).await(Class<T>)`가 같은 response payload를 반환한다.
- `send(...).submit()`이 stage를 반환한다.
- `send(...).await()`가 stage completion을 기다린다.
- `waitFor(name).submit(Class<T>)`가 options의 `requestTimeout`을 기본 timeout으로 사용한다.
- `waitFor(name).timeout(custom).submit(Class<T>)`가 custom timeout을 사용한다.
- `waitFor(name).where(...).submit(Class<T>)`가 predicate false message를 건너뛴다.
- `where` predicate exception이 returned stage failure로 전달된다.
- `MANUAL` dispatch mode에서는 `dispatch().submit()` 또는 `dispatch().await()` 전까지 wait
  handler가 실행되지 않는다.
- JSON, MessagePack, Protobuf connector test가 같은 request/wait API를 사용한다.

### Kotlin wrapper tests

- `request(...).await<T>()`가 `Class<T>` 없이 response를 반환한다.
- `connect().await()`가 Java lifecycle `submit()`을 coroutine suspension으로 기다린다.
- `send(...).await()`가 Java send `submit()`을 coroutine suspension으로 기다린다.
- `waitFor<T>(name).where { ... }.await()`가 matching message를 coroutine suspension으로 기다린다.
- `waitFor<T>(name).await()`가 coroutine cancellation을 전달한다.
- Java stage failure가 Kotlin exception으로 전달된다.
- Kotlin sample compile에서 `SomeClass::class.java` 반복 사용이 client scenario에 남지 않는다.
- Kotlin sample source scan에서 client scenario의 직접 `submit()` 호출이 남지 않는다.

### bindings/java dispatcher tests

- dispatcher가 poller ready socket에서 `DONT_WAIT` recv를 호출한다.
- dispatcher가 여러 message를 batch로 읽고 callback 순서를 유지한다.
- handler callback executor가 native wait thread와 분리된다.
- socket close 중 dispatcher stop이 deadlock 없이 끝난다.
- native recv error가 Java exception 또는 terminal event로 변환된다.
- blocking recv API test는 유지하되 scalable framework path test와 분리한다.

### Java framework handler tests

- virtual thread executor에서 stream handler가 실행된다.
- fixed thread pool executor 옵션도 동작한다.
- handler exception이 protocol별 error path로 변환된다.
- shutdown 중 새 handler 접수가 멈춘다.
- 진행 중 handler가 shutdown timeout 안에 정리된다.
- native wait thread 이름 또는 marker가 handler thread와 다르다는 것을 테스트로 확인한다.

### Kotlin framework handler tests

- suspend handler가 configured dispatcher에서 실행된다.
- handler coroutine cancellation이 response timeout 또는 disconnect와 충돌하지 않는다.
- framework-owned scope shutdown이 모든 child job을 정리한다.
- user-owned scope를 넘긴 경우 framework가 외부 scope 자체를 cancel하지 않는다.

### Sample release tests

- Java Bingo client compile과 self-check
- Java TicTacToe client compile과 self-check
- Kotlin Bingo client compile과 self-check
- Kotlin TicTacToe client compile과 self-check
- sample source scan: `submitAsync`, `waitForAsync`, `SampleAsync`, `SessionGateway` 금지
- sample source scan: default timeout 반복 지정 금지
- sample source scan: business request를 숨기는 helper 금지

## 7.2 Goal 완료 게이트

각 goal은 아래 게이트를 순서대로 통과해야 완료할 수 있다. 앞 게이트가 실패하면 뒤 게이트로
넘어가지 않는다.

### Gate 1. Public surface 검색

public API를 바꾼 goal은 old API 이름이 사용자-facing 표면에 남지 않았는지 검색한다.

필수 검색:

```bash
rg -n "connectAsync\\(|disconnectAsync\\(|reconnectAsync\\(|closeAsync\\(\\)|dispatchAsync\\(|awaitConnect\\(|awaitDisconnect\\(|awaitReconnect\\(|awaitClose\\(|awaitDispatch\\(" \
  framework/languages/java/zlink-stream-connector \
  framework/languages/java/zlink-framework-kotlin \
  framework/languages/java/samples \
  framework/languages/java/doc/spec/stream-connector.ko.md
```

허용 예외:

- transport 내부 구현의 `ZLinkTlsTransportConnection.connectAsync`
- transport 내부 구현의 `ZLinkWebSocketTransportConnection.connectAsync`
- 아직 같은 goal에서 고치기로 명시한 draft 문서

허용 예외가 아닌 결과가 나오면 goal은 완료가 아니다.

### Gate 2. Sample style 검색

sample cleanup goal은 sample client scenario에 금지된 표현이 남지 않았는지 확인한다.

필수 검색:

```bash
rg -n "submitAsync\\(|waitForAsync\\(|SampleAsync|SessionGateway|TicTacToeClientResult" \
  framework/languages/java/samples/java \
  framework/languages/java/samples/kotlin \
  framework/languages/java/zlink-framework-testkit/src/contractTest
```

Kotlin sample에서는 사용자-facing client scenario에 직접 `submit()`이 남아 있으면 안 된다.
단, wrapper 내부 구현과 Java sample은 예외다.

```bash
rg -n "\\.submit\\(" \
  framework/languages/java/samples/kotlin/Bingo/Client/src/main/kotlin \
  framework/languages/java/samples/kotlin/TicTacToe/Client/src/main/kotlin
```

위 검색은 빈 결과가 기준이다. connector lifecycle 정리를 위해 Kotlin Program에 임시로
`close().submit().await()`가 남아 있다면 같은 goal 안에서 Kotlin lifecycle wrapper
`close().await()`를 제공하고 sample을 다시 정리해야 한다.

### Gate 3. 문서 위치 확인

문서를 고친 goal은 내용이 알맞은 디렉토리에 들어갔는지 확인한다.

| 내용 | 위치 |
|------|------|
| 구현 전 API 설계 | `framework/doc/plan/` 또는 `framework/languages/java/doc/draft/` |
| 구현 완료 public API 계약 | `framework/languages/java/doc/spec/` |
| 사용자가 따라 하는 흐름 | `framework/languages/java/doc/guide/` |
| native wait, dispatcher, thread model | `doc/internals/` 또는 언어별 `doc/internals/` |
| sample scenario 기준 | `framework/doc/spec/sample/` |

구현되지 않은 API가 정식 spec에 들어가 있으면 goal은 완료가 아니다. 사용법 설명이 spec에 길게
들어가 있거나 native socket wiring이 guide에 들어가 있어도 완료가 아니다.

### Gate 4. 회귀 테스트 실행

goal 범위에 따라 아래 명령 중 필요한 것을 실행한다. 한 goal에서 여러 범위를 건드렸으면 해당
명령을 모두 실행한다.

| 변경 범위 | 최소 검증 |
|-----------|-----------|
| stream connector API | `./gradlew :zlink-stream-connector:test` |
| codec helper | `./gradlew :zlink-stream-connector-json:test`와 codec contract test |
| Kotlin wrapper | `./gradlew :zlink-framework-kotlin:compileKotlin :zlink-framework-kotlin:compileTestKotlin` |
| Java sample client | `../gradlew -p . :java:Bingo:Client:compileJava :java:TicTacToe:Client:compileJava` |
| Kotlin sample client | `../gradlew -p . :kotlin:Bingo:Client:compileKotlin :kotlin:TicTacToe:Client:compileKotlin` |
| sample release gate | `./gradlew :zlink-framework-testkit:contractTest --tests systems.zlink.framework.testkit.SampleReleaseGateContractTest` |
| bindings dispatcher | bindings/java unit test와 새 dispatcher contract test |
| handler dispatcher | Java/Kotlin framework handler execution test |

Gradle이 native bindings jar를 동시에 건드려 실패하면 병렬 실행 결과를 실패 원인으로 단정하지
않고 같은 명령을 순차 실행해 확인한다. 순차 실행도 실패하면 코드 문제로 본다.

### Gate 5. 최종 리뷰

goal 종료 직전에 아래를 확인한다.

- `git diff --check`가 통과한다.
- public API 변경과 문서 변경이 같은 의미를 말한다.
- sample release gate가 새 style을 검사한다.
- Kotlin wrapper가 Java blocking `await()`를 직접 호출하지 않는다.
- framework handler execution 경로가 native blocking recv를 직접 호출하지 않는다.
- known issue가 있으면 다음 goal의 선행 조건으로 옮겼다.

이 게이트를 통과하지 못하면 goal을 완료로 표시하지 않는다.

## 8. 리뷰 체크리스트

구현 뒤 아래 항목이 모두 닫혀야 한다.

- sample client scenario가 `.NET`처럼 request, wait, ensure 순서로 읽히는가
- helper 함수가 business flow를 숨기지 않는가
- `submitAsync` 이름이 client scenario에 남아 있지 않은가
- `waitFor` 기본 timeout이 options의 `requestTimeout`을 따르는가
- 기본값과 다른 timeout만 scenario code에 보이는가
- codec 변경이 request/wait call shape를 바꾸지 않는가
- Java handler virtual thread가 native blocking recv를 직접 수행하지 않는가
- Kotlin handler가 coroutine dispatcher와 cancellation 의미를 가진가
- bindings 문서가 low-level blocking API와 scalable dispatch path를 구분하는가
- 정식 spec 문서에 구현 전 API가 들어가지 않았는가

## 9. 남은 결정 사항

아래 결정은 구현 전에 두 가지 이상 대안을 비교한 뒤 확정한다.

- bindings/java dispatcher API 이름과 위치
- Java framework 기본 handler executor를 virtual thread로 둘지, 명시 옵션으로만 둘지
- Kotlin 기본 dispatcher와 scope ownership
- blocking recv API misuse를 release gate, static check, guide 중 어디까지 강제할지
- Java/Kotlin sample에서 typed request/wait를 어느 수준까지 Kotlin extension으로 줄일지
