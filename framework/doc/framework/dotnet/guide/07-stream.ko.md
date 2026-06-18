<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Actor · Session Actor Dispatch](06-actor-session.ko.md) | [다음: Registry — topology 조회](08-registry.ko.md)
<!-- framework-adapter-nav:end -->

# 7. STREAM — 외부 client 받기

> 서버 측 정식 계약은 [spec/aspnet-core-stream](../spec/aspnet-core-stream.ko.md),
> client connector 는 [samples/streaming-client](samples/streaming-client.ko.md)와
> [Unity 가이드](../../../../../core/doc/guide/unity-stream-connector.ko.md)가 다룬다.
>
> 🔰 STREAM·session·connector 용어가 낯설면 [03-concepts §0](03-concepts.ko.md)
> 한 줄 풀이를 먼저 본다.

`STREAM` 은 외부 client(게임 클라이언트, 모바일 앱 등)와 서버 사이의 **연결 지향
양방향 메시지 채널**이다. 일반 channel messaging 과 달리 연결 수명, peer 식별,
packet framing, session lifecycle 이 주축이다.

STREAM 은 두 부분으로 나뉜다.

- **서버**: framework session(`Systems.Zlink.Framework`) — `ZLink*` 타입
- **client**: Stream Connector(`Systems.Zlink.Stream.Connector`) — `Zlink*` 타입

> 이름 표기가 다른 이유는 [01-overview](01-overview.ko.md) §7 참고. connector 는
> 서버 framework 패키지에 의존하지 않는 독립 client 라이브러리다.

## 1. 서버 측 — framework session

### 등록

stream node 하나에 session 하나를 붙인다.

```csharp
builder.Services.AddZLinkFramework(options =>
{
    options.Codecs.Use(ZLinkProtobufCodec.Default);

    {
        var stream =     options.AddStreamNode("client.stream");
        stream.Bind("tcp://0.0.0.0:9100");
        stream.RegisterSession<ClientHeaderSession>();

    }
});
```

- packet 의 header binary 포맷은 framework 와 connector 가 공유하는 **고정 내부
  프로토콜**이다. 응용이 framing 을 바꿀 설정은 없다.
- 한 stream node 에 header session 을 둘 이상 등록하면 시작 단계 예외다.
- attribute 기반 등록은 없다(명시 등록만).

### session 작성

session 은 `IZLinkSession` 을 구현한다. framework 가 frame 을 디코드해
`ZlinkStreamHeader header` + `Message payload` 두 부분으로 콜백한다. 응용은
`header.Name` 으로 분기하고 payload 를 타입으로 디코드한다.
framework 가 수신 payload 의 해제를 책임지므로 session callback 안에서는
payload 를 그대로 읽거나 다른 framework API 에 넘기면 된다. callback 뒤에도
payload 를 보관할 때만 `Copy()` 또는 `Move()` 를 사용한다.
application 이 직접 만든 `Message` 를 raw `IZLinkStream.Write(...)` 에 넘기는
경우에는 호출자가 그 `Message` 의 수명을 계속 책임진다. 일반적인 응답과 push 는
`Context.Client.Reply(...)`, `Context.Client.Send(...)`, actor 의 `BoundSession.Send(...)` 를
쓰면 이 수명 규칙을 직접 다룰 일이 없다.
여러 session 전용 packet 을 나누어 처리해야 하면
`IZLinkSessionPacketDispatcher<TSessionContext>` 를 주입받아 등록된 packet 만
handler 로 보낼 수 있다. dispatcher 는 미등록 packet 을 자동 처리하지 않고
`false` 를 반환한다. 그 뒤에 무시, 오류, actor relay 중 무엇을 할지는 session 이
정한다.

```csharp
public sealed class ClientHeaderSession(
    IZLinkSessionContext context,
    IZLinkChannelClient channels) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;
    public ValueTask OnDisconnectedAsync(CancellationToken ct) => ValueTask.CompletedTask;

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken ct)
    {
        int? errno = error.Diagnostic?.NativeCode;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header, Message payload, CancellationToken ct)
    {
        switch (header.Name)
        {
            case "ClientInput":
                var input = payload.Decode<ClientInput>();
                await channels.SendToChannel("play", new ForwardInputCommand(input)).Async(ct);
                break;

            case "Ping":
                var ping = payload.Decode<Ping>();
                await context.Client.Reply(new Pong(ping.Sequence)).Async();
                break;
        }
    }
}
```

`IZLinkSessionContext` 로 할 수 있는 일:

| 표면 | 용도 |
|------|------|
| `Client.Send(msg).Async()` / `Client.Reply(msg).Async()` | client 로 push / 요청에 응답 |
| `Actors.Bound` / `BindAsync(...)` / `Actors.Find(...)` / `IZLinkSessionActor.RelayAsync(...)` | actor 로 relay([06-actor-session](06-actor-session.ko.md)) |
| `CloseAsync()` | 인증 실패/프로토콜 위반 시 서버가 연결 종료 |

다른 서비스로 channel send/request 를 보내야 할 때는 session 생성자에서
`IZLinkChannelClient` 를 함께 주입받아 `SendToChannel(channelName, ...)` 또는
`RequestToChannel(channelName, ...)` 를 호출한다. 이 호출은 현재 stream 연결을 사용하지 않고,
등록된 channel 의 client socket 을 사용한다.

### lifecycle 과 실행 보장

- 콜백은 socket monitor 이벤트에 매핑된다: `OnConnectedAsync` ← connection ready,
  `OnDisconnectedAsync` ← disconnected. session 에 귀속되는 transport 오류는
  `OnErrorAsync` 가 먼저, 연결 종료 확정 후 `OnDisconnectedAsync` 가 따른다.
- handshake 실패와 bind/accept/close 같은 socket 레벨 오류는 session 콜백이 아니라
  runtime monitoring 으로만 간다([09-monitoring](09-monitoring.ko.md)).
- **같은 session 의 콜백은 직렬**로 돈다(두 dispatch/lifecycle 이 겹치지 않음).
  frame 도착 순서는 session 별로 보존된다. session 끼리는 독립으로 진행한다.
- application handler 예외는 `OnErrorAsync` 로 올라오지 않는다.

> **recv 루프는 노출하지 않는다.** 서버 측은 application 이 recv 루프를 직접
> 돌리지 않는다. framework 가 수신 dispatch 를 소유하고 응용은 handler 만
> 구현한다(DI/filter/logging 을 일관되게 엮기 위해).

### 보내기와 직렬화

- `IZLinkStream.Write(Message payload, ...)` 는 backpressure 를 `false` 반환으로
  표현하며 caller payload 를 소비하지 않는다. 보통은 `Send`/`Reply`/`BoundSession`
  를 쓴다.
- payload 디코드는 transport core 에 섞지 않고 등록된 codec 에 위임한다. 위 예제의
  `payload.Decode<T>()` 처럼 codec helper 로 `Message` 를 타입으로 풀고, 타입
  특성으로 codec 을 고른다(생성된 protobuf 타입 → protobuf, 그 외 POCO → json).
  핫패스에서는 `Message.AsReadOnlySpan()` 기반 helper 를 쓰고 `ToArray()` 복사를
  피한다.

## 2. client 측 — Stream Connector

| 패키지 | 역할 |
|--------|------|
| `Systems.Zlink.Stream.Connector` | TCP/TLS/WS/WSS transport + packet connector core |
| `Zlink.Framework.Codecs.MessagePack` / `Zlink.Framework.Codecs.Protobuf` | framework, connector, HTTP client가 공유하는 codec extension |

connector의 JSON codec은 기본값이다. MessagePack이나 Protobuf가 필요하면 framework codec
extension을 등록하고, 같은 extension 인스턴스를 connector typed payload codec으로도 사용한다.
custom codec도 같은 방식으로 `IZLinkCodecExtension`과 stream payload codec 구현을 함께 제공한다.
server framework 쪽 등록(`Codecs.Use(...)`)과 대칭이며, 두 표면의 전체 목록은
[framework-api §2.2](../../common/spec/framework-api.ko.md) 표를 본다.

### 연결과 dispatch

connector 는 만들고(연결 안 함) → 핸들러/이벤트 등록 → `Connect.Async()` →
`Dispatch.Async()` 펌프 순서로 쓴다.

```csharp
using Systems.Zlink.Stream.Connector;

var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
{
    Endpoint = new Uri("tcp://game.example.com:9100"),
    RequestTimeout = TimeSpan.FromSeconds(5),
    DispatchMode = ZlinkStreamDispatchMode.Manual,   // 콜백을 내가 펌프
});

// 수신 핸들러 등록 (이름 기준)
connector.On<GameStateNotify>("GameStateNotify", (msg, ct) =>
{
    Render(msg.Payload);
    return ValueTask.CompletedTask;
});
connector.Disconnected += (ct) => { ShowReconnecting(); return ValueTask.CompletedTask; };

await connector.Connect.Async(cancellationToken);

// 게임 루프/메인 스레드에서 주기적으로 콜백 실행
while (running)
{
    await connector.Dispatch.Async(cancellationToken);
    await Task.Delay(16, cancellationToken);
}
```

- URI scheme 으로 transport 가 추론된다: `tcp://`, `tls://`, `ws://`, `wss://`.
- `ZlinkStreamDispatchMode.Manual`(기본)은 수신/재연결/콜백을 큐에 쌓아 두고, 응용이
  `Dispatch.Async()` 를 부른 스레드에서 실행한다. UI 스레드/게임 루프가 있는 client
  는 반드시 `Manual` 을 쓴다. `Immediate` 는 내부 worker 에서 바로 실행한다.
- 네트워크 수신 루프는 느린 콜백에 막히지 않는다. 패킷을 읽어 콜백 work item 만
  큐에 넣고 다음 읽기로 넘어간다.

### send / request

```csharp
// 단방향 send
await connector
    .Send(new ChatMessage("hello"))
    .Async(cancellationToken);

// 요청-응답
var reply = await connector
    .Request(new GetProfileRequest(accountId))
    .Async<GetProfileReply>(cancellationToken);

// 큰 payload 명시 압축 (LZ4)
await connector
    .Send(new UploadReplayChunk(bytes))
    .Compress()
    .Async(cancellationToken);
```

- 기본 packet 이름은 namespace 없는 CLR 타입 이름. `[ZlinkStreamPacketName("...")]`
  또는 `.PacketName(...)` 으로 override.
- request/response 는 `request_seq` 로 correlate 된다. 응답/오류 packet 이름은 원
  요청과 같아야 한다.

### heartbeat / reconnect

기본값으로 heartbeat(1s 간격, 5s timeout)와 자동 reconnect(초기 250ms, 최대 5s,
backoff 2.0, 최대 3회)가 켜져 있다.

- reconnect 중 submit 은 큐에 쌓이지 않고 `Disconnected` 로 실패한다.
- disconnect 시 대기 중인 모든 request 가 실패하고 reconnect 후 자동 재전송되지
  않는다. 재전송은 응용 책임이다.

### TLS

```csharp
Endpoint = new Uri("wss://game.example.com:443"),
// 운영에서는 SkipServerCertificateValidation 를 켜지 않는다(테스트 자가서명 전용).
```

### Unity

별도 Unity connector 패키지는 없다. Unity 는 `Systems.Zlink.Stream.Connector` core
를 그대로 쓰고 `MonoBehaviour.Update()` 에서 `Dispatch.Async()` 를 호출한다. 그러면
수신 handler 와 lifecycle 이벤트가 Unity 메인 스레드에서 돈다.

비동기 실행과 coroutine adapter의 의미는
[framework 공통 정책](../../common/spec/async-execution-policy.ko.md)을 따른다.
Unity에서도 connector 호출은 일반 `.NET`과 같은 `Task` / `ValueTask` 기반 비동기 API다.
코루틴 중심 프로젝트는 application helper에서 awaitable 호출을 감싼다. 자세한 예제는
[Unity Stream Connector 가이드](../../../../../core/doc/guide/unity-stream-connector.ko.md).

## 3. 오류 코드와 결과

- client 의 `Async(...)` 종결자는 실패 시 `ZlinkStreamException`(`ZlinkStreamError`)을
  던지고, `Submit(callback)` 요청 API 는 `ZlinkStreamResult` 실패를 callback 에 넘긴다.
  remote error 나 사용자 callback 오류는 `ErrorReceived` 이벤트로도 알린다. error code
  의미는 같다.
- 주요 코드: `Disconnected`, `RequestTimeout`, `ConnectTimeout`, `FrameTooLarge`,
  `FrameDecodeFailed`, `TlsValidationFailed`, `RemoteError` 등.
- 서버가 `kind=Error` 로 응답했고 request id 가 없으면 `RemoteError` 로 전달된다.

> 서버 측 `ZLinkStreamError`(대문자 L, `Error`+`Diagnostic`)와 client 측
> `ZlinkStreamError`(소문자 l, `Code`+`Message`)는 **서로 다른 타입**이다. 모양과
> 표기가 다르니 혼동하지 않는다.

## 4. 자주 막히는 곳

- **콜백이 안 불린다(client)** → `ZlinkStreamDispatchMode.Manual` 인데 `Dispatch.Async()` 를
  주기적으로 안 부르고 있다.
- **`FrameTooLarge`** → 송신은 `MaxSendPayloadSize`(기본 64KB), 수신은
  `MaxReceivePayloadSize`(기본 64KB)를 넘었다. 송신 한도는 압축 전 원본 크기로 검사하고,
  수신 한도는 frame payload 크기와 LZ4 압축 해제 결과 크기에 적용한다.
- **압축이 한쪽만** → server→client 는 typed API 가 자동 해제하지만, client→server
  는 `.Compress()` 를 명시해야 한다. LZ4 만 지원.
- **session 콜백에서 actor 상태 직접 접근** → 하지 않는다. session 은 actor
  dispatch/spot 호출만 제출한다([06-actor-session](06-actor-session.ko.md)).

## 5. 더 보기

- 이 챕터 계약의 실행 검증 예문(session/context/push/bound session): [11-interface-catalog](11-interface-catalog.ko.md) §5 — 검증 클래스 `StreamContracts`
- 서버 정식 계약: [spec/aspnet-core-stream](../spec/aspnet-core-stream.ko.md)
- 전체 예제: [STREAM 샘플](samples/stream-samples.ko.md), [Stream Connector 가이드](samples/streaming-client.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Actor · Session Actor Dispatch](06-actor-session.ko.md) | [다음: Registry — topology 조회](08-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
