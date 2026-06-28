<!-- framework-adapter-nav:start -->
[문서 목록](../../../README.ko.md) | [이전: Session Actor Dispatch](07-actor-session.ko.md) | [다음: Registry — topology 조회](09-registry.ko.md)
<!-- framework-adapter-nav:end -->

# 8. STREAM — 외부 client 받기

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
    options.Codecs.Use(ZLinkProtobufCodec.Default);  // client connector 측 payload codec 과 대칭으로 등록해야 한다
    options.ConfigureStreamCompression()
        .UseLz4(); // compressed frame 을 보낼 때와 받을 때 사용할 server codec

    var stream = options.AddStreamNode("client.stream");
    stream.Bind("tcp://0.0.0.0:9100");
    stream.RegisterSession<ClientHeaderSession>();  // node 당 session 1개 — 둘 이상 등록하면 startup 예외
});
```

- 압축 설정을 생략하면 LZ4가 기본값이다. 이 기본값은 모든 frame 을 자동 압축한다는
  뜻이 아니다. send/reply call 에서 `.Compress()` 를 호출한 frame 만 압축된다.
- compressed frame 을 주고받는 connector 와 server 는 같은 compression codec 을 설정해야 한다.
- packet 의 header binary 포맷은 framework 와 connector 가 공유하는 **고정 내부
  프로토콜**이다. 응용이 framing 을 바꿀 설정은 없다.
- 한 stream node 에 header session 을 둘 이상 등록하면 시작 단계 예외다.
- attribute 기반 등록은 없다(명시 등록만).

### session 작성

session 은 `IZLinkSession` 을 구현한다. framework 가 frame 을 디코드해
`ZLinkSessionDispatchContext dispatch` + `ZLinkMessage payload` 두 부분으로 콜백한다. 응용은
`dispatch.PacketName` 으로 분기하고 `payload.Decode<T>()` 로 DTO를 얻는다.
`ZLinkMessage` 는 framework runtime 이 등록된 codec registry와 함께 소유하는 payload
표면이다. session callback은 필요한 packet만 decode하고, actor relay처럼 decode를 미룰 수
있는 경계에는 그대로 넘긴다. application 은 STREAM header나 binding `Message`를 직접 만들지
않고, 일반적인 응답과 push 는 framework helper 를 쓴다(어떤 helper 가 그런지는 아래 코드
주석 참고).
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
        ZLinkSessionDispatchContext dispatch, ZLinkMessage payload, CancellationToken ct)
    {
        switch (dispatch.PacketName)
        {
            case "ClientInput":
                var input = payload.Decode<ClientInput>();
                await channels.SendToChannel("play", new ForwardInputCommand(input)).Async(ct);
                break;

            case "Ping":
                var ping = payload.Decode<Ping>();
                // Client.Reply: 응답 helper. payload 수명을 framework 가 관리한다(Client.Send, actor 의 BoundSession.Send 도 동일).
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
| `Actors.Bound` / `BindAsync(...)` / `Actors.Find(...)` / `IZLinkSessionActor.RelayAsync(...)` | actor 로 relay([06-actor-spot](06-actor-spot.ko.md)) |
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
  runtime monitoring 으로만 간다([10-monitoring](10-monitoring.ko.md)).
- **같은 session 의 콜백은 직렬**로 돈다(두 dispatch/lifecycle 이 겹치지 않음).
  frame 도착 순서는 session 별로 보존된다. session 끼리는 독립으로 진행한다.
- application handler 예외는 `OnErrorAsync` 로 올라오지 않는다.

> **recv 루프는 노출하지 않는다.** 서버 측은 application 이 recv 루프를 직접
> 돌리지 않는다. framework 가 수신 dispatch 를 소유하고 응용은 handler 만
> 구현한다(DI/filter/logging 을 일관되게 엮기 위해).

### 보내기와 직렬화

- `IZLinkStream.Send(ZLinkMessage)` 와 `IZLinkStream.Reply(ZLinkMessage)` 는 send/reply call을
  돌려준다. 응용은 call에서 packet name, metadata, compression을 지정하고 `Async()`로 제출한다.
- session dispatch payload 는 `ZLinkMessage` 로 들어온다. 응용 코드는
  `payload.Decode<T>()` 로 DTO를 얻고, framework runtime 이 등록된 codec registry 로
  JSON, MessagePack, Protobuf, custom codec 을 고른다. codec 을 바꿔도 session handler
  코드는 바꾸지 않는다.

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

// 수신 핸들러·이벤트는 Connect 전에 등록한다 (이름 기준)
connector.On<GameStateNotify>("GameStateNotify", (msg, ct) =>
{
    Render(msg.Payload);
    return ValueTask.CompletedTask;
});
connector.Disconnected += (ct) => { ShowReconnecting(); return ValueTask.CompletedTask; };

await connector.Connect.Async(cancellationToken);   // 여기서 실제 연결 — Create 는 연결하지 않는다

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
- `Manual` 모드에서는 네트워크 수신 루프가 느린 콜백에 막히지 않는다. 패킷을 읽어
  콜백 work item 만 큐에 넣고 다음 읽기로 넘어간다. `Immediate` 모드는 수신 루프가
  콜백 실행을 그 자리에서 기다리므로 느린 콜백이 다음 읽기를 늦춘다.

### 한 번만 기다리기 — `WaitFor<T>()` / `ReceivedCount`

`On<T>(...)` 는 들어오는 packet 마다 계속 불리는 **상시 핸들러**다. 반면 특정 push 가 *한 번* 오기를
기다렸다가 그 값을 받고 싶을 때는 `WaitFor<T>()` 를 쓴다. request/response 가 아니라, server 가 먼저
보내는 push 한 건을 기다리는 용도다(이 push 의 server 쪽 끝점은 보통 actor 의 `BoundSession.Send`
또는 session 의 `Client.Send` 다 — [07 §3](07-actor-session.ko.md)).

```csharp
// 이 push 가 오면 받아서 진행 — Timeout 까지 안 오면 예외
var notify = await connector.WaitFor<ActorPushNotify>()
    .Timeout(TimeSpan.FromSeconds(30))
    .Async(cancellationToken);
```

`ReceivedCount("PacketName")` 는 그 이름으로 **지금 received store 에 남아 있는** packet 수를
돌려준다(누적 합계가 아니라, `WaitFor` 로 가져가면 줄어들고 store 한도를 넘으면 오래된 것부터
빠진다). "이 client 는 그 push 를 받지 *않았어야* 한다(== 0)" 같은 검증/진단에 쓴다.

```csharp
// 예: 이 session 으로는 push 가 오지 않았음을 확인
ScenarioAssert.That(connector.ReceivedCount("ActorPushNotify") == 0, "push 가 오면 안 된다");
```

> `WaitFor`/`ReceivedCount` 는 connector 가 들고 있는 **bounded received store** 위에서 동작한다.
> 크기는 `ZlinkStreamConnectorOptions.MaxReceivedMessages`(기본 1024)로 정하고, 한도를 넘으면 오래된
> 것부터 버린다. payload 크기 한도(`MaxReceivePayloadSize`)와는 별개 설정이다.

### send / request

```csharp
// 단방향 send
await connector
    .Send(new ChatMessage("hello"))
    .Async(cancellationToken);

// 요청-응답: 응답은 request_seq 로 correlate 된다(packet 이름은 매칭에 안 씀)
var reply = await connector
    .Request(new GetProfileRequest(accountId))
    .Async<GetProfileReply>(cancellationToken);

// 큰 payload 명시 압축 — client→server 압축은 .Compress() 로 명시해야 적용된다(server→client 는 자동 해제)
await connector
    .Send(new UploadReplayChunk(bytes))
    .Compress()
    .Async(cancellationToken);
```

- 기본 packet 이름은 namespace 없는 CLR 타입 이름. `[ZlinkStreamPacketName("...")]`
  또는 `.PacketName(...)` 으로 override.
- request/response 는 `request_seq` 로 correlate 된다. packet 이름은 correlation 에
  쓰지 않고, `request_seq` 와 kind(Response/Error)로만 pending request 를 짝짓는다.

### metadata 로 라우팅 — 어느 spot/actor/node 로 보낼지 지정

STREAM client 는 서버의 session 으로 메시지를 보낸다. session 은 메시지에 붙은 문자열
**metadata** 를 읽고, 그 메시지를 어느 **spot·actor·node** 로 전달할지 정한다. client 는
`.Metadata(key, value)` 로 metadata 를 붙이고, 같은 호출에 여러 번 체이닝할 수 있다.

```csharp
// 특정 spot(room) 으로 보내기 — key 이름은 애플리케이션이 정한 상수
await client.Send(new TimerStartCommand(requestId))
    .PacketName("TimerStartCommand")
    .Metadata("spot-rid", spotRid)            // 이 명령을 받을 room 의 spotRid
    .Async();

// 특정 actor 로 보내기
var reply = await client.Request(new ActorYieldReq(requestId))
    .Metadata("actor-id", actorId)            // session 에 bind 된 여러 actor 중 하나를 지정
    .Async<ActorYieldReply>();
```

서버 session 은 dispatch 컨텍스트에서 같은 key 로 값을 읽어 라우팅을 결정한다.

```csharp
// session.OnDispatchAsync 안 — metadata 를 읽어 해당 spot 으로 relay
var spotRid = dispatch.Metadata.Find("spot-rid");
if (string.IsNullOrWhiteSpace(spotRid))
    throw new InvalidOperationException("spot-rid metadata is required.");

// "spot.route" = spot 으로 가는 RouteMesh channel 이름(앱이 등록한 이름).
// 운영 코드는 보통 이 호출을 discovery 경합 대비 재시도로 감싼다.
await routes.Send("spot.route", RoutingId.From(spotRid), command)
    .PacketName(dispatch.PacketName)
    .Async(ct);
```

- **key 는 애플리케이션 규약**이다. framework 가 `"spot-rid"` 같은 이름을 강제하지
  않으니, client·server 가 같은 상수를 공유하면 된다(예: `"spot-rid"`, `"actor-id"`,
  `"target-node-rid"`).
- actor 가 session 에 **하나만** bind 돼 있으면 `actor-id` 를 생략하고 그 actor 로
  보내도록 server 에서 처리할 수 있다. 여럿이면 어느 actor 인지 metadata 로 지정해야
  한다.
- payload(packet)는 "무엇을 할지", metadata 는 "어디로 보낼지"를 나른다 — 둘을
  섞지 않는 게 읽기 편하다.

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
  `request_seq` 가 붙은 remote error 는 그 pending request 의 실패로 전달된다.
  `request_seq` 없는 remote error 와 사용자 callback 오류는 `ErrorReceived` 이벤트로
  알린다. error code 의미는 같다.
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
  수신 한도는 frame payload 크기와 압축 해제 결과 크기에 적용한다.
- **압축이 한쪽만** → server→client 는 typed API 가 자동 해제하지만, client→server
  는 `.Compress()` 를 명시해야 한다. built-in LZ4 또는 custom compression codec 중
  connector 와 server 에 같은 codec 을 설정해야 한다.
- **session 콜백에서 actor 상태 직접 접근** → 하지 않는다. session 은 actor
  dispatch/spot 호출만 제출한다([06-actor-spot](06-actor-spot.ko.md)).

## 5. 더 보기

- 이 챕터 계약의 실행 검증 예문(session/context/push/bound session): [12-interface-catalog](12-interface-catalog.ko.md) §5 — 검증 클래스 `StreamContracts`
- 서버 정식 계약: [spec/aspnet-core-stream](../spec/aspnet-core-stream.ko.md)
- 전체 예제: [STREAM 샘플](samples/stream-samples.ko.md), [Stream Connector 가이드](samples/streaming-client.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../../README.ko.md) | [이전: Session Actor Dispatch](07-actor-session.ko.md) | [다음: Registry — topology 조회](09-registry.ko.md)
<!-- framework-adapter-nav:bottom:end -->
