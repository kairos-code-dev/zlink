<!-- framework-adapter-nav:start -->
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](01-system-structure.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](README.ko.md)

[Java 묶음](../../../../java/README.ko.md) | [STREAM](01-system-structure.ko.md) | [STREAM 가이드](../../../../java/guide/07-stream.ko.md) | [Samples](../../../../../../languages/java/samples/README.md)

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

    ZLinkStreamLifecycleCall connect();
    ZLinkStreamLifecycleCall disconnect();
    ZLinkStreamLifecycleCall reconnect();
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

Java API에서 `submit(...)`은 비동기 작업을 시작하고 `CompletionStage`를 반환한다.
Java connector는 같은 작업을 현재 thread에서 기다리는 별도 blocking terminator를 제공하지 않는다.
lifecycle도 `connect().submit()`, `dispatch().submit()`처럼 같은 call builder 규칙을 따른다.
Kotlin wrapper는 `submit()`으로 얻은
`CompletionStage`를 coroutine suspension으로 기다린다. 이 실행 의미는
[framework 공통 정책](../../04-async-execution-policy.ko.md)을 따른다.

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
없으면 type의 `SimpleName`을 사용한다. 호출별 override는 허용하지 않는다. 이미 encode한
raw payload의 identity는 `ZLinkStreamEncodedPayload.packetName()`에 명시한다.

metadata는 작은 key-value만 담는다. 큰 업무 데이터는 payload로 보낸다.
STREAM wire header는 runtime 내부 타입이다. connector 사용자와 server session은 header
객체를 만들거나 전달하지 않고, packet name과 metadata snapshot만 공개 모델에서 다룬다.

## 7. Send와 Request

```java
public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(Map<String, String> metadata);
    ZLinkStreamSendCall compress();
    void submit();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(Map<String, String> metadata);
    ZLinkStreamRequestCall timeout(Duration timeout);
    ZLinkStreamRequestCall compress();
    CompletionStage<ZLinkStreamEncodedPayload> submit();
    <TReply> CompletionStage<TReply> submit(Class<TReply> replyType);
}

public interface ZLinkTypedStreamSendCall {
    ZLinkTypedStreamSendCall metadata(String key, String value);
    ZLinkTypedStreamSendCall metadata(Map<String, String> metadata);
    ZLinkTypedStreamSendCall compress();
    void submit();
}

public interface ZLinkTypedStreamRequestCall {
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
thread에서 `dispatch().submit()`을 호출한다.

`AUTO`는 내부 worker 흐름에서 callback을 바로 실행한다. UI thread나 game loop가
있는 client sample은 `MANUAL`을 유지한다.

## 10. 연결 상태

상태의 의미와 전이는 [공통 스펙 §6](../../32-stream-connector.ko.md)이 소유한다. Java는 닫힌 enum으로
표현한다.

```java
public enum ZLinkStreamConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    CLOSED
}
```

`close().submit()`이 완료된 뒤에는 `CLOSED`이고 **새 `connect()`는 실패한다.**

> ⚠️ **공통 계약과 다르다.** 공통 스펙은 초기 상태 `Created`("생성됐고 아직 연결하지 않음")를
> 규정하지만 **Java enum에는 `CREATED`가 없다.** "한 번도 연결한 적 없음"과 "끊김"이 같은
> `DISCONNECTED`가 된다. [구현 차이 §10.6](../../90-implementation-gap.ko.md)이 이 gap을 소유한다.

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
    fun disconnect(): ZLinkKotlinLifecycleCall
    fun reconnect(): ZLinkKotlinLifecycleCall
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
[문서 목록](../../../../../README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](01-system-structure.ko.md)
<!-- framework-adapter-nav:bottom:end -->
