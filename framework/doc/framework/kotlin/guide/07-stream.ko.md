<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Actor/Session](06-actor-session.ko.md) | [다음: Registry](08-registry.ko.md)
<!-- framework-adapter-nav:end -->

# Kotlin STREAM Guide

`STREAM`은 외부 client(게임 클라이언트, 모바일 앱 등)와 서버 사이의 **연결 지향
양방향 메시지 채널**이다. 일반 channel messaging과 달리 연결 수명, peer 식별,
packet framing, session lifecycle이 주축이다. STREAM은 두 부분으로 나뉜다.

- **서버**: framework session — `ZLinkSuspendingSession`
- **client**: Stream Connector — `ZLinkStreamConnector` (server framework에
  의존하지 않는 독립 client 모듈). Kotlin에서는 `connector.kotlin()`으로 coroutine·
  `Flow` 표면을 얻는다.

## 1. 서버 측 — framework session

### 등록

stream node 하나에 session 하나를 붙인다. 한 stream node에 session을 둘 이상
등록하면 startup 단계 예외다. 명시 등록만 쓴다.

```kotlin
ZLinkFrameworkConfigurer { options ->
    options.useCoroutineHandlers(Dispatchers.Default)
    options.codecs().use(ZLinkProtobufCodec.defaultCodec())
    options.configureStreamCompression {
        useLz4() // 이 stream server가 compressed frame을 보낼 때와 받을 때 사용할 codec
    }

    val stream = options.addStreamNode("client.stream")
    stream.bind("tcp://0.0.0.0:9100")
    stream.registerSession(GameSession::class.java)
}
```

압축 설정을 생략하면 LZ4가 기본값이다. 이 기본값은 모든 frame을 자동으로 압축한다는 뜻이 아니다.
응용이 send/reply call에서 compression을 요청한 frame만 압축된다. compressed frame을 주고받는
connector와 server는 같은 compression codec을 설정해야 한다.

### session 작성

server framework는 dispatch context 기반 session 하나를 사용한다. packet session과 raw
session을 public type으로 나누지 않는다. framework가 frame을 디코드해
`ZLinkSessionDispatchContext dispatch` + `ZLinkMessage payload` 두 부분으로 콜백한다. Kotlin에서는
`ZLinkSuspendingSession`을 상속해 `onDispatchSuspending`을 `suspend`로 둔다.

```kotlin
class GameSession(
    private val context: ZLinkSessionContext,
    private val channels: ZLinkClient,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage) {
        when (dispatch.packetName()) {
            "ClientInput" -> {
                val input = payload.decode(ClientInput::class.java)
                channels.sendToChannel("play", ForwardInputCommand(input)).submit()
            }
            "Ping" -> context.client().reply(Pong()).submit()
            else -> return
        }
    }
}
```

`ZLinkSessionContext`로 할 수 있는 일:

| 표면 | 용도 |
|------|------|
| `client().send(msg).submit()` / `client().reply(msg).submit()` | client로 push / 요청에 응답 |
| `actors().bound()` / `actors().bind(...)` / `actors().find(...)` | actor로 relay([06-actor-session](06-actor-session.ko.md)) |
| `close()` | 인증 실패나 protocol 위반 시 서버 연결 종료 요청 |

다른 서비스로 channel send/request를 보내야 할 때는 session 생성자에서
`ZLinkClient`를 함께 주입받아 `send(...)`/`request<T>(...)` suspend 확장을 호출한다. 이
호출은 현재 stream 연결이 아니라 등록된 channel의 client socket을 쓴다.

### lifecycle과 실행 보장

- 콜백은 socket monitor 이벤트에 매핑된다: `onConnectedSuspending` <- connection ready,
  `onDisconnectedSuspending` <- disconnected. session에 귀속되는 transport 오류는
  `onErrorSuspending`가 먼저, 연결 종료 확정 후 `onDisconnectedSuspending`가 따른다.
- handshake 실패와 bind/accept/close 같은 socket 레벨 오류는 session 콜백이 아니라
  runtime monitoring으로만 간다([09-monitoring](09-monitoring.ko.md)).
- **같은 session의 콜백은 직렬**로 실행된다(두 dispatch/lifecycle이 겹치지 않음).
  frame 도착 순서는 session별로 보존된다. session끼리는 독립으로 진행한다.
- application handler 예외는 `onErrorSuspending`로 올라오지 않는다.
- **recv 루프는 노출하지 않는다.** framework가 수신 dispatch를 소유하고 응용은
  handler만 구현한다(DI/filter/logging을 일관되게 엮기 위해).

## 2. client 측 — Stream Connector

connector는 만들고(연결 안 함) -> `.kotlin()`으로 coroutine view를 얻고 -> 이벤트
`Flow`를 collect -> `connect()` 순서로 쓴다. lifecycle과 request 호출은
`suspend fun await()`로 결과를 기다릴 수 있지만, client push인 send는 `submit()`으로
제출하고 송신 수락 완료를 기다리지 않는다.

```kotlin
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import systems.zlink.framework.kotlin.kotlin

val connector = ZLinkStreamConnectorFactory.create(options).kotlin()

// 들어오는 packet을 Flow로 구독
launch {
    connector.messages("GameStateNotify").collect { message ->
        render(message)
    }
}

connector.connect().await()           // suspend, non-blocking
connector.send(payload).submit()
```

connector 쪽도 같은 codec을 설정한다. Kotlin 확장은 기존 Java options를 복사해 compression 설정만
바꾼다.

```kotlin
val connector = ZLinkStreamConnectorFactory.create(
    ZLinkStreamConnectorOptions.createDefault(URI.create("tcp://127.0.0.1:9100"))
        .withLz4StreamCompression(), // server의 configureStreamCompression 설정과 맞춘다
).kotlin()
```

custom compression을 쓸 때도 server와 connector에 같은 구현을 넣는다.

```kotlin
val codec = MyStreamCompressionCodec()

ZLinkFrameworkConfigurer { options ->
    options.configureStreamCompression {
        use(codec) // 이 server runtime의 활성 compression codec
    }
}

val connector = ZLinkStreamConnectorFactory.create(
    ZLinkStreamConnectorOptions.createDefault(URI.create("tcp://127.0.0.1:9100"))
        .withStreamCompression(codec), // 같은 codec으로 압축된 frame만 복원할 수 있다
).kotlin()
```

요청-응답형은 `waitFor`/`request`의 suspend `await()`를 쓴다.

```kotlin
val state: GameStateNotify =
    connector.waitFor<GameStateNotify>()
        .timeout(Duration.ofSeconds(5))
        .await()
        .payload()   // await는 ZLinkStreamMessage<GameStateNotify>를 반환한다
```

- URI scheme으로 transport가 추론된다: `tcp://`, `tls://`, `ws://`, `wss://`.
- 네트워크 수신 루프는 느린 콜백에 막히지 않는다. 패킷을 읽어 `Flow`로 흘려보내고
  다음 읽기로 넘어간다. `messages(...)`/`errors()`는 `callbackFlow` 기반이라 collect를
  멈추면 등록이 자동 해제된다.
- 기본값으로 heartbeat와 자동 reconnect가 켜져 있다. disconnect 시 대기 중인
  모든 request가 실패하고 reconnect 후 자동 재전송되지 않는다(재전송은 응용 책임).
- `tls://`와 `wss://`는 기본값으로 서버 인증서와 호스트명을 검증한다.
  `skipServerCertificateValidation`은 테스트용 자체 서명 인증서에만 쓰며, 운영 환경에서는
  사용하지 않는다.

connector는 server framework와 별도 모듈이며 TCP/TLS/WS/WSS, reconnect, codec
helper를 제공한다.

connector도 framework처럼 **custom codec**을 끼울 수 있다. `ZLinkStreamConnectorOptions`의
`typedCodec`에 `ZLinkStreamCodec`(`encode(packetName, value)`/`decode(payload, type)`) 구현을
주면 Avro·Thrift 같은 포맷을 쓴다. server framework 쪽 등록(`codecs().use(extension)`)과
대칭이며, 두 표면의 전체 목록은
[framework-api §9](../../spec/05-framework-api.ko.md#9-codec) 표를 본다.

payload codec과 compression codec은 서로 다른 설정이다. payload codec은 DTO와 bytes 사이를 바꾸고,
compression codec은 이미 만들어진 bytes를 전송 전에 압축하거나 수신 후 복원한다.

## 3. 자주 막히는 곳

- **`Flow`가 안 흐른다(client)** -> `messages(...)`를 collect하는 coroutine을 launch
  하지 않았거나 `connect().await()` 전에 취소됐다.
- **한 stream node에 session 둘** -> startup 예외. node 하나에 session 하나다.
- **session 콜백에서 actor 상태 직접 접근** -> 하지 않는다. session은 actor
  dispatch/spot 호출만 제출한다([06-actor-session](06-actor-session.ko.md)).
- **handler 안에서 blocking** -> `suspend` 콜백 안에서 `Thread.sleep`/blocking I/O를
  직접 쓰지 않는다. 불가피하면 `withContext(Dispatchers.IO)`로 옮긴다.

## 4. 더 보기

- session을 actor에 묶기: [06-actor-session](06-actor-session.ko.md)
- client connector 상세: [stream-connector](../../spec/stream-connector/languages/java/03-stream-connector.ko.md)
- 서버 정식 계약: [spring-boot-stream](../../spec/server/languages/java/01-system-structure.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Actor/Session](06-actor-session.ko.md) | [다음: Registry](08-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
