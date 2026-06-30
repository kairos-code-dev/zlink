<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../README.ko.md) | [STREAM](spring-boot-stream.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md) | [Samples](../../../../languages/java/samples/README.md)

# Java/Kotlin Stream Connector

## 1. 목표

Stream Connector는 server framework와 별도 모듈이다. 서버의 `ZLinkSession`이 받는
framework header 기반 STREAM packet을 외부 client가 같은 방식으로 만들고 해석하게
한다.

이 모듈은 Spring Boot server adapter, SPOT, Registry에 의존하지 않는다. transport,
codec, compression, reconnect, dispatch queue처럼 client 실행에 필요한 의존성만 가진다.

## 2. 모듈

| 모듈 | 역할 |
|------|------|
| `zlink-stream-connector` | TCP/TLS/WS/WSS transport, frame codec, send/request, dispatch |
| `zlink-framework-kotlin` | coroutine, `Flow`, DSL extension |
| `zlink-framework-codec-protobuf` | framework/connector/http-client에서 공유하는 Protobuf codec extension |
| `zlink-framework-codec-msgpack` | framework/connector/http-client에서 공유하는 MessagePack codec extension |

JSON은 framework 기본 codec이다. Protobuf와 MessagePack payload는 connector 전용 package가
아니라 `zlink-framework-codec-protobuf`, `zlink-framework-codec-msgpack` framework codec
extension을 connector에도 적용해서 사용한다.

## 3. Public API

```java
public interface ZLinkStreamConnector {
    boolean isConnected();
    ZLinkStreamConnectionState state();
    ZLinkStreamConnectorOptions options();
    int pendingDispatchCount();
    int receivedCount(String name);

    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall disconnect();
    ZLinkStreamLifecycleCall reconnect();
    ZLinkStreamLifecycleCall close();
    ZLinkStreamLifecycleCall dispatch();
    <T> T await(CompletionStage<T> stage) throws Exception;

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);
    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);
    ZLinkStreamSendCall send(Object payload);
    ZLinkStreamRequestCall request(Object payload);
    ZLinkStreamWaitCall waitFor(String name);
    ZLinkStreamWaitCall waitFor(Class<?> payloadType);

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);
    <TPayload> AutoCloseable on(
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler);
    <TPayload> AutoCloseable on(
        String name,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler);
    AutoCloseable onErrorReceived(ZLinkStreamErrorHandler handler);
    AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler);
    AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler);
    AutoCloseable observeInbound(ZLinkStreamInboundObserver observer);
}

public interface ZLinkStreamLifecycleCall {
    CompletionStage<Void> submit();
    void await() throws Exception;
}

public final class ZLinkStreamConnectorFactory {
    public static ZLinkStreamConnector create(ZLinkStreamConnectorOptions options);
}
```

Java는 event를 `on...` registration으로 노출한다. .NET의 event와 의미는 같다.
등록 해제는 반환된 `AutoCloseable`로 한다.
`waitFor(...)`는 특정 packet name의 server push를 한 번 기다리는 call builder를 반환한다.
필요한 message만 고를 때는 builder의 `where(...)`를 사용한다. timeout이 지나면 반환된
`CompletionStage`가 timeout 실패로 끝난다. 별도 timeout을 지정하지 않으면 connector
options의 `waitTimeout()` 값을 사용한다. `MANUAL` dispatch mode에서는 caller가
기존처럼 `dispatch().submit()` 또는 `dispatch().await()`를 호출해야 wait handler가 실행된다.

Java API에서 `submit(...)`은 비동기 작업을 시작하고 `CompletionStage`를 반환한다.
`await(...)`는 같은 작업의 완료를 현재 thread에서 기다린 뒤 결과를 반환한다. lifecycle도
`connect().submit()`, `connect().await()`, `dispatch().submit()`, `dispatch().await()`
처럼 같은 call builder 규칙을 따른다. Kotlin wrapper는 `submit()`으로 얻은
`CompletionStage`를 coroutine suspension으로 기다린다. 이 실행 의미는
[framework 공통 정책](../../common/spec/async-execution-policy.ko.md)을 따른다.

## 4. Options

```java
// transport(TCP/TLS/WS/WSS)는 endpoint URI scheme으로 정해진다. heartbeat/reconnect 설정은
// 별도 nested 객체가 아니라 flat field다. `createDefault(URI endpoint)`로 기본값 인스턴스를 만든다.
public record ZLinkStreamConnectorOptions(
    URI endpoint,
    ZLinkStreamDispatchMode dispatchMode,      // default MANUAL
    Duration requestTimeout,                   // default 30s
    Duration waitTimeout,                      // default 5s
    int maxReconnectAttempts,                  // default 3
    Duration connectTimeout,                   // default 5s
    int maxSendPayloadSize,                    // default 64 * 1024
    int maxReceivePayloadSize,                 // default 64 * 1024
    boolean heartbeatEnabled,                  // default true
    Duration heartbeatInterval,                // default 1s
    Duration heartbeatTimeout,                 // default 5s
    boolean reconnectEnabled,                  // default true
    Duration reconnectInitialDelay,
    Duration reconnectMaxDelay,                // default 5s
    double reconnectBackoffFactor,             // default 2.0
    boolean skipServerCertificateValidation,
    ZLinkStreamCompression compression,
    ZLinkStreamPacketNameResolver nameResolver,
    ZLinkStreamTypedCodec typedCodec) {
}
```

`skipServerCertificateValidation`은 테스트용 자체 서명 인증서에만 사용한다. 운영
기본값은 `false`다. 이 값을 `true`로 바꾸면 TLS transport와 WSS transport 모두 서버
인증서를 신뢰하지 않고 통과시키므로, 운영 환경에서는 사용하면 안 된다.

## 5. Transport와 codec

transport(TCP/TLS/WS/WSS)는 별도 enum 옵션이 아니라 endpoint URI scheme
(`tcp://`/`tls://`/`ws://`/`wss://`)으로 선택된다.

```java
public enum ZLinkStreamCodec {
    RAW,
    JSON,
    MESSAGE_PACK,
    PROTOBUF
}

public enum ZLinkStreamCompression {
    NONE,
    LZ4
}
```

URI scheme에서 transport를 추론한다.

| URI scheme | transport |
|------------|-----------|
| `tcp://` | `TCP` |
| `tls://` | `TLS` |
| `ws://` | `WEB_SOCKET` |
| `wss://` | `WEB_SOCKET_SECURE` |

TLS transport는 기본값에서 서버 인증서 체인과 호스트명을 모두 검증한다. 호스트명 검증은
`HTTPS` endpoint identification 규칙을 사용한다.

## 6. Packet 모델

```java
public record ZLinkStreamEncodedPayload(
    String packetName,
    Message payload,
    Map<String, String> metadata,
    ZLinkStreamCodec codec) {
}

public record ZLinkStreamMessage<TPayload>(
    String packetName,
    TPayload payload,
    Map<String, String> metadata) {
}
```

packet name 해석 순서는 아래와 같다.

1. builder의 `packetName(...)`
2. payload type의 `@ZLinkStreamPacketName`
3. payload type의 `SimpleName`

metadata는 작은 key-value만 담는다. 큰 업무 데이터는 payload로 보낸다.
STREAM wire header는 runtime 내부 타입이다. connector 사용자와 server session은 header
객체를 만들거나 전달하지 않고, packet name과 metadata snapshot만 공개 모델에서 다룬다.

## 7. Send와 Request

```java
public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String name);
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(Map<String, String> metadata);
    ZLinkStreamSendCall compress();
    void submit();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall packetName(String name);
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkStreamRequestCall timeout(Duration timeout);
    ZLinkStreamRequestCall compress();
    CompletionStage<ZLinkStreamEncodedPayload> submit();
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
    <TReply> TReply await(Class<TReply> replyType) throws Exception;
}

public interface ZLinkStreamWaitCall {
    ZLinkStreamWaitCall timeout(Duration timeout);
    ZLinkStreamWaitCall where(
        Predicate<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> predicate);
    <TPayload> ZLinkStreamWaitCall where(
        Class<TPayload> payloadType,
        Predicate<ZLinkStreamMessage<TPayload>> predicate);
    CompletionStage<ZLinkStreamMessage<ZLinkStreamEncodedPayload>> submit();
    <TPayload> CompletionStage<ZLinkStreamMessage<TPayload>> submit(
        Class<TPayload> payloadType);
    <TPayload> ZLinkStreamMessage<TPayload> await(Class<TPayload> payloadType)
        throws Exception;
}
```

request timeout이 끝나면 pending request를 제거한다. response가 늦게 도착하면
request stage를 완료하지 않는다.
현재 in-memory smoke connector는 등록된 reply handler가 없는 request를 즉시 timeout
실패로 드러낸다. 이 동작은 sample이 timeout을 sleep으로 숨기지 않고 pending request
정리 의미를 검증하기 위한 첫 구현 기준이다.

## 8. Typed payload codec

기본 connector는 wire payload를 `ZLinkStreamEncodedPayload`로 보관한다. JSON,
MessagePack, Protobuf, auto codec 모듈은 `.NET` connector extension과 같은 방식으로
codec registry에 등록되고, typed send/request/on/wait 표면이 그 registry를 사용해
업무 DTO를 encode/decode한다. application code는 일반적으로 raw `Message`나 codec
helper를 직접 다루지 않는다.

위의 `ZLinkStreamConnector.send(Object)`, `request(Object)`,
`on(Class<TPayload>, ...)`, `waitFor(...)`가 typed payload 표면이다. codec 선택과
payload encode/decode는 connector가 가진 codec registry 안에서 처리한다.

auto codec extension은 payload type이나 annotation을 보고 codec을 고른다. codec을 고를
수 없으면 configuration error로 실패한다. typed 표면이 만드는 packet name도 core
connector의 name resolver를 그대로 사용한다.
server push를 기다릴 때는 기본 connector의 wait builder를
사용한다. payload 조건이 필요하면
`connector.waitFor(name).where(payloadType, predicate).submit(payloadType)`처럼
core wait builder의 `where`를 사용한다. sample client는 server push를 기다릴 때
connector member `waitFor(...).where(...).submit(...)` 또는 Kotlin wrapper
`waitFor<T>(...).where { ... }.await()` 형태를 사용한다.
typed 표면은 registry가 encode/decode할 수 있는 업무 객체 payload를 기준으로
동작한다. `String`, `byte[]`, `Message` 같은 raw payload는 connector 하위 경로나
명시적 raw 사용에서만 다룬다.

Kotlin extension은 Java typed payload 표면 위에 얇게 얹는다.

```kotlin
// request 응답은 ZLinkStreamRequestCall 의 reified typed await 로 받는다(codec 가 JSON 등 처리).
suspend inline fun <reified TReply> ZLinkStreamRequestCall.await(): TReply

// 구독은 connector 의 waitFor<T>() 또는 messages(packetName) Flow 로 한다.
inline fun <reified TPayload> ZLinkStreamConnector.waitFor(): ZLinkStreamTypedWaitCall<TPayload>

fun ZLinkStreamConnector.messages(
    packetName: String,
): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
```

## 9. Dispatch Mode

```java
public enum ZLinkStreamDispatchMode {
    AUTO,
    MANUAL
}
```

기본값은 `MANUAL`이다. receive loop, reconnect loop, request callback task가 사용자
handler를 직접 호출하지 않고 dispatch queue에 넣는다. application은 자신이 원하는
thread에서 `dispatch().submit()` 또는 `dispatch().await()`를 호출한다.

`AUTO`는 내부 worker 흐름에서 callback을 바로 실행한다. UI thread나 game loop가
있는 client sample은 `MANUAL`을 유지한다.

## 10. 상태와 reconnect

```java
public enum ZLinkStreamConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    CLOSED
}
```

상태 전이는 아래를 기준으로 한다.

```text
DISCONNECTED -> CONNECTING -> CONNECTED
CONNECTED -> RECONNECTING -> CONNECTED
CONNECTED -> DISCONNECTED
DISCONNECTED -> CONNECTING
* -> CLOSED
```

heartbeat timeout이나 transport disconnect가 발생하면 reconnect가 켜져 있을 때
`RECONNECTING`으로 이동한다. `maxAttempts`를 넘으면 `DISCONNECTED`가 된다.
`close().submit()` 또는 `close().await()` 이후에는 `CLOSED`이고, 새 `connect()`는 실패한다.

## 11. Error Code

```java
public enum ZLinkStreamErrorCode {
    DISCONNECTED,
    CONFIGURATION_ERROR,
    VALIDATION_FAILED,
    REQUEST_TIMEOUT,
    CONNECT_TIMEOUT,
    FRAME_DECODE_FAILED,
    FRAME_TOO_LARGE,
    SEND_FAILED,
    COMPRESSION_FAILED,
    TLS_VALIDATION_FAILED,
    DECOMPRESSION_FAILED,
    USER_CALLBACK_FAILED,
    OBSERVER_FAILED,
    OBSERVER_DROPPED,
    REMOTE_ERROR
}
```

callback 실패는 connector runtime을 조용히 중단시키지 않는다. `USER_CALLBACK_FAILED`
error event로 올리고, connector lifecycle은 명시된 상태 전이 규칙을 따른다.

## 12. Inbound Observer

`observeInbound(...)`는 수신 frame을 읽기 전용으로 관찰하는 API다. client code는
connector를 만든 뒤 `connect().submit()` 또는 `connect().await()`를 호출하기 전에
observer를 등록한다. 연결이 시작된 뒤 등록하면 invalid state 오류로 실패한다.

```java
try (AutoCloseable log = connector.observeInbound(observation -> {
    System.out.printf(
        "stream-inbound kind=%s name=%s seq=%s bytes=%d%n",
        observation.kind(),
        observation.packetName(),
        observation.requestSeq(),
        observation.payloadLength());
})) {
    connector.connect().await();
}
```

`ZLinkStreamInboundObservation`은 message kind, packet name, codec, request sequence,
metadata, payload byte length, 압축 여부, 수신 시간, payload preview를 담는다. metadata와
preview는 복사된 값이므로 observer가 값을 바꿔도 dispatch와 pending request 완료에
영향을 주지 않는다. payload preview 기본 길이는 0이다. observer queue 크기는 1024개 notification이다.

observer callback은 receive 경로에서 직접 실행하지 않는다. callback 실패는
`OBSERVER_FAILED`, bounded queue overflow는 `OBSERVER_DROPPED` error event로 보고한다.
두 오류는 관찰 기능의 진단 신호이며 원래 수신 frame의 처리 결과를 바꾸지 않는다.

## 13. Kotlin 표면

Kotlin module은 Java connector 위의 thin wrapper다. Kotlin 사용자 code는 Java `submit()`을
직접 호출하지 않고 Kotlin wrapper의 suspend `await()`를 사용한다. 이 `await()`는 Java blocking
`await()`를 호출하지 않고, Java `submit()`이 반환한 `CompletionStage`를 coroutine suspension으로
기다린다.

```kotlin
fun ZLinkStreamConnector.kotlin(): ZLinkKotlinStreamConnector

class ZLinkKotlinStreamConnector {
    fun connect(): ZLinkKotlinLifecycleCall
    fun disconnect(): ZLinkKotlinLifecycleCall
    fun reconnect(): ZLinkKotlinLifecycleCall
    fun close(): ZLinkKotlinLifecycleCall
    fun dispatch(): ZLinkKotlinLifecycleCall
    fun send(payload: Any): ZLinkKotlinSendCall
    fun request(payload: Any): ZLinkStreamRequestCall
    fun <TPayload> waitFor(): ZLinkStreamTypedWaitCall<TPayload>
    fun <TPayload> waitFor(name: String): ZLinkStreamTypedWaitCall<TPayload>
    fun messages(packetName: String): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
    fun errors(): Flow<ZLinkStreamError>
}

class ZLinkKotlinLifecycleCall {
    suspend fun await()
}

class ZLinkKotlinSendCall {
    suspend fun await()
}

suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload
suspend fun <TReply> ZLinkStreamRequestCall.await(): TReply

class ZLinkStreamTypedWaitCall<TPayload> {
    fun timeout(timeout: Duration): ZLinkStreamTypedWaitCall<TPayload>
    fun where(predicate: (ZLinkStreamMessage<TPayload>) -> Boolean): ZLinkStreamTypedWaitCall<TPayload>
    suspend fun await(): ZLinkStreamMessage<TPayload>
}
```

Kotlin wrapper는 Java connector와 다른 상태 전이나 buffering 정책을 만들지 않는다.
`messages(...)`와 `errors()`는 Java connector의 `on(...)`, `onErrorReceived(...)`
handler를 `callbackFlow`로 감싼다. 따라서 manual dispatch mode에서는 Java와 마찬가지로
Kotlin wrapper의 `dispatch().await()`가 호출되어야 collector가 메시지나 error event를 받는다.

## 14. 검증 기준

Java connector는 아래 테스트를 별도 suite로 가진다.

- public API export test
- transport scheme inference와 mismatch validation
- header encode/decode roundtrip
- metadata validation
- send frame size limit
- request timeout pending cleanup
- manual dispatch queue와 `pendingDispatchCount`
- immediate dispatch callback
- heartbeat ping/pong과 timeout
- reconnect backoff와 max attempts
- typed handler registry add/remove
- JSON, MessagePack, Protobuf codec smoke
- typed helper packet name resolver와 codec selection
- typed request/reply decode
- inbound observer response/send/control 관찰, callback 실패, queue overflow
- Kotlin coroutine/Flow wrapper smoke

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](spring-boot-stream.ko.md)
<!-- framework-adapter-nav:bottom:end -->
