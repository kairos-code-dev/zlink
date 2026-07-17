<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: Actor/Session](06-actor-session.ko.md) | [다음: Registry](08-registry.ko.md)
<!-- framework-adapter-nav:end -->

# Java STREAM Guide

`STREAM`은 외부 client(게임 클라이언트, 모바일 앱 등)와 서버 사이의 **연결 지향
양방향 메시지 채널**이다. 일반 channel messaging과 달리 연결 수명, peer 식별,
packet framing, session lifecycle이 주축이다. STREAM은 두 부분으로 나뉜다.

- **서버**: framework session — `ZLinkSession`
- **client**: Stream Connector — `ZLinkStreamConnector` (server framework에
  의존하지 않는 독립 client 모듈)

## 1. 서버 측 — framework session

### 등록

stream node 하나에 session 하나를 붙인다. 한 stream node에 session을 둘 이상
등록하면 startup 단계 예외다. attribute 기반 등록은 없다(명시 등록만).

```java
@Override
public void configure(ZLinkFrameworkOptions framework) {
    framework.codecs().use(ZLinkProtobufCodec.defaultCodec());
    framework.configureStreamCompression()
        .useLz4(); // compressed frame을 보낼 때와 받을 때 사용할 server codec

    ZLinkStreamNodeBuilder stream = framework.addStreamNode("client.stream");
    stream.bind("tcp://0.0.0.0:9100");
    stream.registerSession(GameSession.class);
}
```

압축 설정을 생략하면 LZ4가 기본값이다. 이 기본값은 모든 frame을 자동 압축한다는
뜻이 아니다. 응용이 send/reply call에서 `compress()`를 호출한 frame만 압축된다.
compressed frame을 주고받는 connector와 server는 같은 compression codec을 설정해야 한다.

### session 작성

Java server framework는 dispatch context 기반 `ZLinkSession` 하나를 사용한다. packet session과
raw session을 public type으로 나누지 않는다. framework가 frame을 디코드해
`ZLinkSessionDispatchContext dispatch` + `ZLinkMessage payload` 두 부분으로 콜백한다. 응용은
`dispatch.packetName()`으로 분기하고 `payload.decode(...)`로 DTO를 얻는다.

```java
@Component
public final class GameSession implements ZLinkSession {
    private final ZLinkSessionContext context;
    private final ZLinkClient channels;

    @Override public CompletionStage<Void> onConnected() {
        return CompletableFuture.completedFuture(null);
    }
    @Override public CompletionStage<Void> onDisconnected() {
        return CompletableFuture.completedFuture(null);
    }
    @Override public CompletionStage<Void> onError(ZLinkStreamError error) {
        return CompletableFuture.completedFuture(null);
    }

    public GameSession(ZLinkSessionContext context, ZLinkClient channels) {
        this.context = context;
        this.channels = channels;
    }

    @Override
    public ZLinkSessionContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onDispatch(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload) {
        switch (dispatch.packetName()) {
            case "ClientInput":
                ClientInput input = payload.decode(ClientInput.class);
                channels.sendToChannel("play", new ForwardInputCommand(input))
                    .submit();
                return CompletableFuture.completedFuture(null);
            case "Ping":
                context.client().reply(new Pong()).submit();
                return CompletableFuture.completedFuture(null);
            default:
                return CompletableFuture.completedFuture(null);
        }
    }
}
```

`ZLinkSessionContext`로 할 수 있는 일:

| 표면 | 용도 |
|------|------|
| `client().send(msg).submit()` / `client().reply(msg).submit()` | client로 push / 요청에 응답 |
| `actors().bound()` / `actors().bind(...)` / `actors().find(...)` | actor로 relay([06-actor-session](06-actor-session.ko.md)) |
| `close()` | 인증 실패/프로토콜 위반 시 서버가 연결 종료 |

다른 서비스로 channel send/request를 보내야 할 때는 session 생성자에서
`ZLinkClient`를 함께 주입받아 `sendToChannel(...)` 또는 `requestToChannel(...)`을
호출한다. 이 호출은 현재 stream 연결이 아니라 등록된 channel의 client socket을 쓴다.

### lifecycle과 실행 보장

- 콜백은 socket monitor 이벤트에 매핑된다: `onConnected` <- connection ready,
  `onDisconnected` <- disconnected. session에 귀속되는 transport 오류는
  `onError`가 먼저, 연결 종료 확정 후 `onDisconnected`가 따른다.
- handshake 실패와 bind/accept/close 같은 socket 레벨 오류는 session 콜백이 아니라
  runtime monitoring으로만 간다([09-monitoring](09-monitoring.ko.md)).
- **같은 session의 콜백은 직렬**로 실행된다(두 dispatch/lifecycle이 겹치지 않음).
  frame 도착 순서는 session별로 보존된다. session끼리는 독립으로 진행한다.
- application handler 예외는 `onError`로 올라오지 않는다.
- **recv 루프는 노출하지 않는다.** framework가 수신 dispatch를 소유하고 응용은
  handler만 구현한다(DI/filter/logging을 일관되게 엮기 위해).

## 2. client 측 — Stream Connector

connector는 만들고(연결 안 함) -> 핸들러/이벤트 등록 -> `connect()` ->
`dispatch()` 펌프 순서로 쓴다. UI 스레드/게임 루프가 있는 client는 수신
콜백을 `dispatch()`를 부른 스레드에서 실행하도록 manual dispatch를 쓴다.

```java
ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(options);

connector.on("GameStateNotify", (message) -> {
    render(message);
    return CompletableFuture.completedFuture(null);
});

connector.connect().submit()
    .thenRun(() -> connector.send(payload).submit());

// 게임 루프/메인 스레드에서 주기적으로 콜백 실행
while (running) {
    connector.dispatch().submit().whenComplete((ignored, error) -> {
        if (error != null) {
            reportDispatchError(error);
        }
    });
}
```

- URI scheme으로 transport가 추론된다: `tcp://`, `tls://`, `ws://`, `wss://`.
- 네트워크 수신 루프는 느린 콜백에 막히지 않는다. 패킷을 읽어 콜백 work item만
  큐에 넣고 다음 읽기로 넘어간다.
- 기본값으로 heartbeat와 자동 reconnect가 켜져 있다. disconnect 시 대기 중인
  모든 request가 실패하고 reconnect 후 자동 재전송되지 않는다(재전송은 응용 책임).
- `tls://`와 `wss://`는 기본값으로 서버 인증서와 호스트명을 검증한다.
  `skipServerCertificateValidation`은 테스트용 자체 서명 인증서에만 쓰며, 운영 환경에서는
  사용하지 않는다.

connector는 server framework와 별도 모듈이며 TCP/TLS/WS/WSS, manual dispatch,
reconnect, codec helper를 제공한다.

connector 쪽도 같은 compression codec을 설정한다. 기본 options는 LZ4를 사용한다.
custom compression을 쓰는 경우에는 server framework와 connector 양쪽에 같은 구현을
넣어야 한다.

```java
ZLinkStreamConnectorOptions options = new ZLinkStreamConnectorOptions(
    URI.create("tcp://127.0.0.1:9100")); // 기본 LZ4 compression codec 사용
```

custom compression codec은 `ZLinkStreamConnectorOptions` record 값의 `compressionCodec`에 넣는다. 이때
`compression`은 `LZ4`로 두고 codec 객체만 교체한다. 이 값은 알고리즘 id를 전송한다는
뜻이 아니라, 이 connector runtime이 compressed frame을 처리할 때 사용할 구현을 정한다는
뜻이다.

connector도 framework처럼 **custom codec**을 끼울 수 있다. `ZLinkStreamConnectorOptions`의
`typedCodec`에 `ZLinkStreamCodec`(`encode(packetName, value)`/`decode(payload, type)`) 구현을
주면 Avro·Thrift 같은 포맷을 쓴다. server framework 쪽 등록(`codecs().use(extension)`)과
대칭이며, 두 표면의 전체 목록은
[framework-api §9](../../spec/05-framework-api.ko.md#9-codec) 표를 본다.

payload codec과 compression codec은 서로 다른 설정이다. payload codec은 DTO와 bytes 사이를
바꾸고, compression codec은 이미 만들어진 bytes를 전송 전에 압축하거나 수신 후 복원한다.

## 3. 자주 막히는 곳

- **콜백이 안 불린다(client)** -> manual dispatch인데 `dispatch()`를 주기적으로
  안 부르고 있다.
- **한 stream node에 session 둘** -> startup 예외. node 하나에 session 하나다.
- **session 콜백에서 actor 상태 직접 접근** -> 하지 않는다. session은 actor
  dispatch/spot 호출만 제출한다([06-actor-session](06-actor-session.ko.md)).

## 4. 더 보기

- session을 actor에 묶기: [06-actor-session](06-actor-session.ko.md)
- client connector 상세: [stream-connector](../../spec/stream-connector/languages/java/03-stream-connector.ko.md)
- 서버 정식 계약: [spring-boot-stream](../../spec/server/languages/java/01-system-structure.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../README.ko.md) | [이전: Actor/Session](06-actor-session.ko.md) | [다음: Registry](08-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
