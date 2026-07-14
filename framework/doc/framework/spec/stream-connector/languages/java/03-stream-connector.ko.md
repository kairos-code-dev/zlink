<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](../../../server/languages/java/01-system-structure.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](../../../server/languages/java/README.ko.md)

[Java 묶음](../../../../java/README.ko.md) | [STREAM](../../../server/languages/java/01-system-structure.ko.md) | [STREAM 가이드](../../../../java/guide/07-stream.ko.md) | [Samples](../../../../../../languages/java/samples/README.md)

# Java/Kotlin Stream Connector

> 이 문서는 [Stream Connector 공통 스펙](../../32-stream-connector.ko.md)의 **Java/Kotlin
> 투영**이다. transport·wire·생명주기·오류 의미는 공통 스펙이 소유하고, 이 문서는 그 의미가
> Java/Kotlin에서 갖는 **정확한 public 표면**을 고정한다.

## 1. 목표

Stream Connector는 server framework와 별도 모듈이다. 서버의 `ZLinkSession`이 받는
framework header 기반 STREAM packet을 외부 client가 같은 방식으로 만들고 해석하게
한다.

이 모듈은 Spring Boot server adapter, SPOT, Registry에 의존하지 않는다. transport,
codec, compression, reconnect, dispatch queue처럼 client 실행에 필요한 의존성만 가진다.

### 1.1 대상 실행 환경

**엔진 × 빌드 타깃별 담당 connector는 [공통 스펙 §2](../../32-stream-connector.ko.md)가 소유한다.**
그 배정에 따라 Java/Kotlin connector가 담당하는 것은 **JVM 애플리케이션**(서버 도구·E2E 테스트·
봇)이며, 게임 엔진과 브라우저는 담당하지 않는다.

대상이 하나뿐이라 이 배정이 Java/Kotlin 표면에 남기는 결과는 없다. 엔진별 갈래가 없으므로
별도 가이드 트리도 두지 않는다.

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

    // lifecycle 표면은 셋뿐이다. 수동 재연결은 connect()의 상태 전이이고,
    // 자동 재연결은 options가 담당한다(공통 스펙 32 §6).
    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall close();
    ZLinkStreamLifecycleCall dispatch();

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);
    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);
    ZLinkTypedStreamSendCall send(Object payload);
    ZLinkTypedStreamRequestCall request(Object payload);
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
}

public final class ZLinkStreamConnectorFactory {
    public static ZLinkStreamConnector create(ZLinkStreamConnectorOptions options);
}
```

Java는 event를 `on...` registration으로 노출한다. .NET의 event와 의미는 같다.
등록 해제는 반환된 `AutoCloseable`로 한다.

**세션 종료 사유 (close reason).** 값 집합과 의미는
[공통 스펙 §6.2](../../32-stream-connector.ko.md)가 소유한다. Java는 이를 닫힌 enum
`ZLinkStreamCloseReason`(`CLIENT_CLOSE`, `IDLE_TIMEOUT`, `HEARTBEAT_TIMEOUT`, `SERVER_DRAIN`,
`PROTOCOL_ERROR`, `TRANSPORT_ERROR`)으로 표현하고, **`ZLinkStreamDisconnectedHandler`가 받는
disconnect 이벤트의 `ZLinkStreamCloseReason closeReason()`으로 노출한다.**
`waitFor(...)`는 특정 packet name의 server push를 한 번 기다리는 call builder를 반환한다.
필요한 message만 고를 때는 builder의 `where(...)`를 사용한다. timeout이 지나면 반환된
`CompletionStage`가 timeout 실패로 끝난다. 별도 timeout을 지정하지 않으면 connector
options의 `waitTimeout()` 값을 사용한다. `MANUAL` dispatch mode에서는 caller가
`dispatch().submit()`을 호출해야 wait handler가 실행된다.

Java API에서 `submit(...)`은 비동기 작업을 시작한다. **one-way send의 `submit()`은 완료 객체를
반환하지 않고**(`void`), request·wait·lifecycle의 `submit()`만 `CompletionStage`를 반환한다
([04 §1](../../../04-async-execution-policy.ko.md)).
Java connector는 같은 작업을 현재 thread에서 기다리는 별도 blocking terminator를 제공하지 않는다.
lifecycle도 `connect().submit()`, `dispatch().submit()`처럼 같은 call builder 규칙을 따른다.
Kotlin wrapper는 `submit()`으로 얻은
`CompletionStage`를 coroutine suspension으로 기다린다. 이 실행 의미는
[framework 공통 정책](../../../04-async-execution-policy.ko.md)을 따른다.

## 4. Options

**기본값은 [공통 스펙 §6.1](../../32-stream-connector.ko.md)이 소유한다.** Java는 이를 flat field를
갖는 record로 표현한다(heartbeat·reconnect를 nested 객체로 두지 않는다).
`createDefault(URI endpoint)`로 기본값 인스턴스를 만든다.

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
    int maxReceivedMessages,                   // default 1024 (수신 메시지 큐 상한)
    int maxInboundObserverNotifications,       // default 1024
    int maxInboundObserverPayloadPreviewBytes, // default 0
    boolean heartbeatEnabled,                  // default true
    Duration heartbeatInterval,                // default 1s
    Duration heartbeatTimeout,                 // default 5s
    boolean reconnectEnabled,                  // default true
    Duration reconnectInitialDelay,            // default 250ms
    Duration reconnectMaxDelay,                // default 5s
    double reconnectBackoffFactor,             // default 2.0
    boolean skipServerCertificateValidation,
    ZLinkStreamCompression compression,
    ZLinkStreamCompressionCodec compressionCodec,
    ZLinkStreamPacketNameResolver nameResolver,
    ZLinkStreamTypedCodec typedCodec) {
}
```

`skipServerCertificateValidation`은 테스트용 자체 서명 인증서에만 사용한다. 운영
기본값은 `false`다. 이 값을 `true`로 바꾸면 TLS transport와 WSS transport 모두 서버
인증서를 신뢰하지 않고 통과시키므로, 운영 환경에서는 사용하면 안 된다.

## 5. Transport와 codec

scheme → transport 매핑과 TLS 검증 규칙은 [공통 스펙 §3](../../32-stream-connector.ko.md)이 소유한다.
Java는 **transport를 별도 enum 옵션으로 고르지 않고 endpoint URI scheme으로 추론한다.**

```java
public enum ZLinkStreamTransport { TCP, TLS, WEB_SOCKET, WEB_SOCKET_SECURE }
public enum ZLinkStreamCodec { RAW, JSON, MESSAGE_PACK, PROTOBUF }
public enum ZLinkStreamCompression { NONE, LZ4 }
```

**호스트명 검증은 `HTTPS` endpoint identification 규칙을 사용한다.**

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

typed object의 packet identity는 payload type의 `@ZLinkStreamPacketName`을 우선하고,
없으면 type의 `SimpleName`을 사용한다. **호출자가 `packetName(...)`으로 명시하면 그 이름이
우선한다**(공통 스펙 32 §5). 이미 encode한 raw payload의 identity는
`ZLinkStreamEncodedPayload.packetName()`에 명시한다.

metadata는 작은 key-value만 담는다. 큰 업무 데이터는 payload로 보낸다.
STREAM wire header는 runtime 내부 타입이다. connector 사용자와 server session은 header
객체를 만들거나 전달하지 않고, packet name과 metadata snapshot만 공개 모델에서 다룬다.

## 7. Send와 Request

```java
public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String name);   // 호출별 override. 명시하면 이 이름이 우선한다
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(Map<String, String> metadata);
    ZLinkStreamSendCall compress();
    void submit();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall packetName(String name);   // 호출별 override
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkStreamRequestCall timeout(Duration timeout);
    ZLinkStreamRequestCall compress();
    CompletionStage<ZLinkStreamEncodedPayload> submit();
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}

public interface ZLinkTypedStreamSendCall {
    ZLinkTypedStreamSendCall packetName(String name);   // 호출별 override
    ZLinkTypedStreamSendCall metadata(String key, String value);
    ZLinkTypedStreamSendCall metadata(Map<String, String> metadata);
    ZLinkTypedStreamSendCall compress();
    void submit();
}

public interface ZLinkTypedStreamRequestCall {
    ZLinkTypedStreamRequestCall packetName(String name);   // 호출별 override
    ZLinkTypedStreamRequestCall metadata(String key, String value);
    ZLinkTypedStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkTypedStreamRequestCall timeout(Duration timeout);
    ZLinkTypedStreamRequestCall compress();
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
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
}
```

request timeout이 끝나면 pending request를 제거하고 반환한 `CompletionStage`를 timeout
실패로 완료한다. 제거된 request의 response가 늦게 도착해도 그 stage를 다시 완료하지 않는다.

## 8. Typed payload codec

기본 connector는 wire payload를 `ZLinkStreamEncodedPayload`로 보관한다. typed 표면은
options의 **`typedCodec` 하나**를 사용해 업무 DTO를 encode/decode한다(기본은 JSON).
application code는 일반적으로 raw `Message`나 codec helper를 직접 다루지 않는다.

위의 `ZLinkStreamConnector.send(Object)`, `request(Object)`,
`on(Class<TPayload>, ...)`, `waitFor(...)`가 typed payload 표면이다.

typed 표면이 만드는 packet name은 core connector의 name resolver를 그대로 사용한다.
codec으로 표현할 수 없는 payload는 configuration error로 실패한다.
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
// request 응답은 ZLinkStreamRequestCall 의 reified typed awaitReply 로 받는다(codec 가 JSON 등 처리).
// 이름은 §13의 목표 선언과 같다 -- 비-reified `await()`와 overload로 겹치지 않게 분리한다.
inline suspend fun <reified TReply> ZLinkStreamRequestCall.awaitReply(): TReply

// 구독은 connector 의 waitFor<T>() 또는 messages(packetName) Flow 로 한다.
inline fun <reified TPayload> ZLinkStreamConnector.waitFor(): ZLinkStreamTypedWaitCall<TPayload>

fun ZLinkStreamConnector.messages(
    packetName: String,
): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
```

## 9. Dispatch Mode

```java
public enum ZLinkStreamDispatchMode {
    MANUAL,     // 기본값
    IMMEDIATE   // receive 경로에서 인라인 실행한다(공통 스펙 32 §7)
}
```

기본값은 `MANUAL`이다. receive loop, reconnect loop, request callback task가 사용자
handler를 직접 호출하지 않고 dispatch queue에 넣는다. application은 자신이 원하는
thread에서 `dispatch().submit()`을 호출한다.

`IMMEDIATE`는 receive 경로에서 callback을 인라인 실행하므로, 느린 handler가 receive loop를
막고 그만큼 backpressure가 걸린다. UI thread나 game loop가 있는 client sample은 `MANUAL`을
유지한다.

## 10. 연결 상태

상태의 의미와 전이는 [공통 스펙 §6](../../32-stream-connector.ko.md)이 소유한다. Java는 닫힌 enum으로
표현한다.

```java
public enum ZLinkStreamConnectionState {
    CREATED,
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    CLOSED
}
```

`close().submit()`이 완료된 뒤에는 `CLOSED`이고 **새 `connect()`는 실패한다.**

**`CREATED`는 첫 연결 시도 전의 초기 상태다.** 연결 시도에 실패한 뒤에는 `DISCONNECTED`로
전환하므로, "한 번도 연결한 적 없음"과 "끊김"을 구분한다.

## 11. Error Code

오류의 의미는 [공통 스펙 §9](../../32-stream-connector.ko.md)가 소유한다. Java는 닫힌 enum으로
표현한다.

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
    RECEIVED_MESSAGE_DROPPED,   // 수신 메시지 큐 overflow(공통 스펙 32 §10)
    REMOTE_ERROR
}
```

## 12. Inbound Observer

관찰 의미와 격리·overflow 규칙은 [공통 스펙 §10](../../32-stream-connector.ko.md)이 소유한다.
Java는 **연결 시작 전에만 등록**하고 `AutoCloseable`로 해제한다.

```java
try (AutoCloseable log = connector.observeInbound(observation -> {
    System.out.printf(
        "stream-inbound kind=%s name=%s seq=%s bytes=%d%n",
        observation.kind(),
        observation.packetName(),
        observation.requestSeq(),
        observation.payloadLength());
})) {
    connector.connect().submit(); // 연결 완료는 반환된 CompletionStage로 관찰한다.
}
```

## 13. Kotlin 표면

Kotlin module은 Java connector 위의 thin wrapper다. lifecycle과 request처럼 완료값이
있는 작업은 Kotlin wrapper의 suspend `await()`로 기다린다. 이 `await()`는 Java
`CompletionStage`를 coroutine suspension으로 기다린다. one-way send는 완료 객체를
만들지 않고 `submit()`으로 local queue에 맡긴다.

```kotlin
fun ZLinkStreamConnector.kotlin(): ZLinkKotlinStreamConnector

class ZLinkKotlinStreamConnector {
    fun connect(): ZLinkKotlinLifecycleCall
    fun close(): ZLinkKotlinLifecycleCall
    fun dispatch(): ZLinkKotlinLifecycleCall
    fun send(payload: ZLinkStreamEncodedPayload): ZLinkKotlinSendCall
    fun send(payload: Any): ZLinkKotlinSendCall
    fun request(payload: ZLinkStreamEncodedPayload): ZLinkStreamRequestCall
    fun request(payload: Any): ZLinkTypedStreamRequestCall
    fun <TPayload> waitFor(): ZLinkStreamTypedWaitCall<TPayload>
    fun <TPayload> waitFor(name: String): ZLinkStreamTypedWaitCall<TPayload>
    fun messages(packetName: String): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
    fun errors(): Flow<ZLinkStreamError>
}

class ZLinkKotlinLifecycleCall {
    suspend fun await()
}

class ZLinkKotlinSendCall {
    fun submit()
}

suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload
inline suspend fun <reified TReply> ZLinkStreamRequestCall.awaitReply(): TReply
inline suspend fun <reified TReply> ZLinkTypedStreamRequestCall.awaitReply(): TReply

class ZLinkStreamTypedWaitCall<TPayload> {
    fun timeout(timeout: Duration): ZLinkStreamTypedWaitCall<TPayload>
    fun where(predicate: (ZLinkStreamMessage<TPayload>) -> Boolean): ZLinkStreamTypedWaitCall<TPayload>
    suspend fun await(): ZLinkStreamMessage<TPayload>
}
```

Kotlin wrapper는 Java connector와 다른 상태 전이나 buffering 정책을 만들면 안 된다. options를
복사하는 extension은 **수신 메시지 한도를 포함해 모든 option 값을 보존해야 한다.**
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
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](../../../server/languages/java/01-system-structure.ko.md)
<!-- framework-adapter-nav:bottom:end -->
