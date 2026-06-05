<!-- framework-adapter-nav:start -->
[문서 목록](../../../../doc/README.ko.md) | [이전: ZLink Framework Spring Boot STREAM](./spring-boot-stream.ko.md) | [다음: Java STREAM Decisions And Open Items](../draft/stream-open-items.ko.md)
<!-- framework-adapter-nav:end -->

[Java spec 목차](./README.ko.md)

[Java 묶음](../README.ko.md) | [포팅 계획](../draft/java-kotlin-framework-porting-plan.ko.md) | [STREAM](./spring-boot-stream.ko.md) | [STREAM 샘플](../guide/samples/stream-samples.ko.md) | [Samples](../../samples/README.md)

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
| `zlink-stream-connector-json` | Jackson JSON payload helper |
| `zlink-stream-connector-messagepack` | MessagePack payload helper |
| `zlink-stream-connector-protobuf` | Protobuf payload helper |
| `zlink-stream-connector-codecs` | payload type 기준 codec 선택 helper |
| `zlink-stream-connector-kotlin` | coroutine, `Flow`, DSL extension |

## 3. Public API

```java
public interface ZLinkStreamConnector extends AutoCloseable {
    boolean isConnected();
    ZLinkStreamConnectionState state();
    ZLinkStreamConnectorOptions options();
    int pendingDispatchCount();

    CompletionStage<Void> connectAsync();
    CompletionStage<Void> disconnectAsync();
    CompletionStage<Void> reconnectAsync();
    CompletionStage<Void> closeAsync();
    CompletionStage<Void> dispatchAsync();

    ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload);
    ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload);

    AutoCloseable on(
        String name,
        ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler);

    AutoCloseable onErrorReceived(ZLinkStreamErrorHandler handler);
    AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler);
    AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler);
}

public final class ZLinkStreamConnectorFactory {
    public static ZLinkStreamConnector create(ZLinkStreamConnectorOptions options);
}
```

Java는 event를 `on...` registration으로 노출한다. .NET의 event와 의미는 같다.
등록 해제는 반환된 `AutoCloseable`로 한다.

## 4. Options

```java
public final class ZLinkStreamConnectorOptions {
    URI endpoint();
    Optional<ZLinkStreamTransport> transport();
    Duration connectTimeout();      // default 5s
    Duration requestTimeout();      // default 30s
    ZLinkStreamHeartbeatOptions heartbeat();
    ZLinkStreamReconnectOptions reconnect();
    int maxSendPayloadSize();       // default 64 * 1024
    boolean skipServerCertificateValidation();
    ZLinkStreamDispatchMode dispatchMode(); // default MANUAL
    ZLinkStreamCompression compression();   // default NONE
    ZLinkStreamPacketNameResolver nameResolver();
}

public final class ZLinkStreamHeartbeatOptions {
    boolean enabled();        // default true
    Duration interval();      // default 1s
    Duration timeout();       // default 5s
}

public final class ZLinkStreamReconnectOptions {
    boolean enabled();        // default true
    Duration initialDelay();  // default 250ms
    Duration maxDelay();      // default 5s
    double backoffFactor();   // default 2.0
    OptionalInt maxAttempts(); // default 3
}
```

`skipServerCertificateValidation`은 테스트용 자체 서명 인증서에만 사용한다. 운영
기본값은 `false`다.

## 5. Transport와 codec

```java
public enum ZLinkStreamTransport {
    TCP,
    TLS,
    WEB_SOCKET,
    WEB_SOCKET_SECURE
}

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

`transport`를 명시했는데 endpoint scheme과 어긋나면 configuration error다.

## 6. Packet 모델

```java
public record ZLinkStreamEncodedPayload(
    ZLinkStreamCodec codec,
    byte[] payload,
    @Nullable Class<?> messageType) {
}

public record ZLinkStreamMessage<TPayload>(
    String name,
    ZLinkStreamMetadata metadata,
    TPayload payload) {
}

public record ZLinkStreamHeader(
    ZLinkStreamMessageKind kind,
    ZLinkStreamCodec codec,
    EnumSet<ZLinkStreamHeaderFlag> flags,
    Optional<ZLinkStreamRequestSeq> requestSeq,
    String name,
    ZLinkStreamMetadata metadata) {
}
```

packet name 해석 순서는 아래와 같다.

1. builder의 `packetName(...)`
2. payload type의 `@ZLinkStreamPacketName`
3. payload type의 `SimpleName`

metadata는 작은 key-value만 담는다. 큰 업무 데이터는 payload로 보낸다.

## 7. Send와 Request

```java
public interface ZLinkStreamSendCall {
    ZLinkStreamSendCall packetName(String name);
    ZLinkStreamSendCall metadata(String key, String value);
    ZLinkStreamSendCall metadata(ZLinkStreamMetadata metadata);
    ZLinkStreamSendCall compress();
    CompletionStage<Void> submitAsync();
}

public interface ZLinkStreamRequestCall {
    ZLinkStreamRequestCall packetName(String name);
    ZLinkStreamRequestCall metadata(String key, String value);
    ZLinkStreamRequestCall metadata(ZLinkStreamMetadata metadata);
    ZLinkStreamRequestCall timeout(Duration timeout);
    ZLinkStreamRequestCall compress();
    CompletionStage<ZLinkStreamEncodedPayload> submitAsync();
    AutoCloseable submit(Consumer<ZLinkStreamResult<ZLinkStreamEncodedPayload>> callback);
}
```

request timeout이 끝나면 pending request를 제거한다. response가 늦게 도착하면
사용자 callback을 다시 호출하지 않는다.
현재 in-memory smoke connector는 등록된 reply handler가 없는 request를 즉시 timeout
실패로 드러낸다. 이 동작은 sample이 timeout을 sleep으로 숨기지 않고 pending request
정리 의미를 검증하기 위한 첫 구현 기준이다.

## 8. Typed codec helper

core connector는 `ZLinkStreamEncodedPayload`만 이해한다. JSON, MessagePack,
Protobuf, auto codec 모듈은 `.NET` connector extension과 같은 방식으로 typed helper를
제공한다.

```java
public final class ZLinkStreamJson {
    public static ZLinkStreamSendCall send(
        ZLinkStreamConnector connector,
        Object payload);

    public static ZLinkStreamRequestCall request(
        ZLinkStreamConnector connector,
        Object payload);

    public static <TPayload> AutoCloseable on(
        ZLinkStreamConnector connector,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler);

    public static <TPayload> AutoCloseable on(
        ZLinkStreamConnector connector,
        String name,
        Class<TPayload> payloadType,
        ZLinkStreamMessageHandler<TPayload> handler);
}
```

auto codec helper는 payload type이나 annotation을 보고 codec을 고른다. codec을 고를
수 없으면 configuration error로 실패한다. typed helper가 만드는 packet name도 core
connector의 name resolver를 그대로 사용한다.
첫 구현의 typed helper는 core connector smoke와 같은 `String`, `byte[]`, `Message`
payload를 지원한다. 복합 DTO 직렬화는 JSON/MessagePack/Protobuf 라이브러리 선택과
schema 정책이 닫힌 뒤 확장한다.

Kotlin extension은 typed helper 위에 얇게 얹는다.

```kotlin
suspend inline fun <reified TReply : Any> ZLinkStreamConnector.requestJson(
    payload: Any
): TReply

inline fun <reified TPayload : Any> ZLinkStreamConnector.onJson(
    noinline handler: suspend (ZLinkStreamMessage<TPayload>) -> Unit
): AutoCloseable
```

## 9. Dispatch Mode

```java
public enum ZLinkStreamDispatchMode {
    MANUAL,
    IMMEDIATE
}
```

기본값은 `MANUAL`이다. receive loop, reconnect loop, request callback task가 사용자
handler를 직접 호출하지 않고 dispatch queue에 넣는다. application은 자신이 원하는
thread에서 `dispatchAsync()`를 호출한다.

`IMMEDIATE`는 내부 worker 흐름에서 callback을 바로 실행한다. UI thread나 game loop가
있는 client sample은 `MANUAL`을 유지한다.

## 10. 상태와 reconnect

```java
public enum ZLinkStreamConnectionState {
    CREATED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    DISCONNECTED,
    CLOSED
}
```

상태 전이는 아래를 기준으로 한다.

```text
CREATED -> CONNECTING -> CONNECTED
CONNECTED -> RECONNECTING -> CONNECTED
CONNECTED -> DISCONNECTED
DISCONNECTED -> CONNECTING
* -> CLOSED
```

heartbeat timeout이나 transport disconnect가 발생하면 reconnect가 켜져 있을 때
`RECONNECTING`으로 이동한다. `maxAttempts`를 넘으면 `DISCONNECTED`가 된다.
`closeAsync()` 이후에는 `CLOSED`이고, 새 `connectAsync()`는 실패한다.

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
    REMOTE_ERROR
}
```

callback 실패는 connector runtime을 조용히 중단시키지 않는다. `USER_CALLBACK_FAILED`
error event로 올리고, connector lifecycle은 명시된 상태 전이 규칙을 따른다.

## 12. Kotlin 표면

Kotlin module은 Java connector 위의 thin wrapper다.

```kotlin
suspend fun ZLinkStreamConnector.connect()
suspend fun ZLinkStreamConnector.disconnect()
suspend fun ZLinkStreamConnector.reconnect()
suspend fun ZLinkStreamConnector.closeConnector()
suspend fun ZLinkStreamSendCall.submit()
suspend fun ZLinkStreamRequestCall.await(): ZLinkStreamEncodedPayload
fun ZLinkStreamConnector.messages(packetName: String): Flow<ZLinkStreamMessage<ZLinkStreamEncodedPayload>>
fun ZLinkStreamConnector.errors(): Flow<ZLinkStreamError>
```

Kotlin wrapper는 Java connector와 다른 상태 전이나 buffering 정책을 만들지 않는다.
`messages(...)`와 `errors()`는 Java connector의 `on(...)`, `onErrorReceived(...)`
handler를 `callbackFlow`로 감싼다. 따라서 manual dispatch mode에서는 Java와 마찬가지로
`dispatchAsync()`가 호출되어야 collector가 메시지나 error event를 받는다.

## 13. 검증 기준

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
- Kotlin coroutine/Flow wrapper smoke
