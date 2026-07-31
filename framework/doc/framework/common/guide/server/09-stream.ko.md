# 9. STREAM

> 서버의 정확한 시그니처는 [언어별 STREAM session 공개 계약](../../../common/spec/server/languages/README.ko.md)가
> 정의한다. Client package는 Stream Connector 가이드와
> [언어별 공개 계약](../../../common/spec/stream-connector/README.ko.md)을 따른다.

STREAM은 외부 client와 Framework server 사이의 연결 지향 양방향 메시지 채널이다.
Server는 session lifecycle과 packet dispatch를 구현한다. Client는 독립 package인
`Systems.Zlink.Stream.Connector`를 사용한다.

## 1. Server node 등록

Stream node에는 session type 하나를 등록한다. Actor dispatch를 사용하면 명시적으로 활성화한다.

=== "C#/.NET"

    ```csharp
    options.AddStreamNode("client-stream")
        .Bind("tcp://0.0.0.0:9100")
        .EnableActorDispatch()
        .AddSession<PlaySession>(); // 연결마다 만들 session type을 등록한다.
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


Session handler와 Actor/Spot handler는 Framework의 기본 typed JSON serialization을 사용한다.
Application이 message type마다 codec을 등록하거나 raw frame을 해석하지 않는다.

## 2. Session lifecycle

Session은 연결, packet dispatch, 오류와 disconnect callback을 구현한다. 같은 session의 callback은
직렬로 실행된다.

=== "C#/.NET"

    ```csharp
    public sealed class PlaySession(
        IZLinkSessionContext context,
        ILogger<PlaySession> logger) : IZLinkSession
    {
        public IZLinkSessionContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddHandler<PingHandler>(); // typed session packet handler를 등록한다.
        }

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
        {
            logger.LogInformation("connected: {SessionId}", Context.SessionId);
            return ValueTask.CompletedTask;
        }

        public async ValueTask OnDispatchAsync(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload,
            CancellationToken cancellationToken)
        {
            if (!await Context.Handlers.TryHandleAsync(
                    dispatch,
                    payload,
                    cancellationToken))
            {
                await Context.CloseAsync(); // application protocol에 없는 packet을 받으면 연결을 닫는다.
            }
        }

        public ValueTask OnErrorAsync(
            ZLinkStreamError error,
            CancellationToken cancellationToken)
        {
            logger.LogWarning(
                "session error: {Error} {Message}",
                error.Error,
                error.Message);
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            logger.LogInformation("disconnected: {SessionId}", Context.SessionId);
            return ValueTask.CompletedTask;
        }
    }
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


Handshake와 node 범위 오류는 runtime monitoring으로 보고한다. Session `OnErrorAsync`는 session
범위 오류만 받는다.

## 3. Typed packet handler

Handler registry가 `ZLinkMessage`를 typed message로 decode한다. Request에 reply할 때는 현재 dispatch의
one-shot reply token을 사용한다.

=== "C#/.NET"

    ```csharp
    public sealed class PingHandler
        : IZLinkSessionPacketHandler<IZLinkSessionContext, Ping>
    {
        public async ValueTask HandleAsync(
            IZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            Ping message,
            CancellationToken cancellationToken)
        {
            if (!dispatch.CanReply)
            {
                throw new InvalidOperationException("Ping must be a request.");
            }

            await context.Client
                .Reply(new Pong(message.Sequence))
                .Async(cancellationToken); // 같은 request correlation으로 한 번만 reply한다.
        }
    }
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


`Reply`는 현재 request에서만 유효하며 한 번 제출할 수 있다. Timeout이나 cancellation으로 전송이
실패해도 같은 reply token을 다시 사용할 수 없다.

Server가 먼저 push할 때는 `Send`를 사용한다.

=== "C#/.NET"

    ```csharp
    await Context.Client
        .Send(new ServerNotice("maintenance"))
        .Metadata("severity", "info")
        .Compress()
        .Async(cancellationToken); // local transport queue admission까지 기다린다.
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


## 4. Actor dispatch

인증 뒤 Actor를 session에 bind하고, session 전용 handler가 처리하지 않은 `ZLinkMessage`를
`IZLinkSessionActor.RelayAsync`로 넘길 수 있다. 상세 흐름은
[Session과 Actor binding](08-actor-session.ko.md)을 따른다.

Application은 session route를 Location Store에서 직접 조회하지 않는다. Actor relocation이 완료되면
Framework가 binding route를 갱신한다.

## 5. Client 연결

Client는 server Framework package가 아니라 Stream Connector package를 사용한다.

=== "C#/.NET"

    ```csharp
    await using var connector = ZlinkStreamConnectorFactory.Create(
        new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri("tcp://game.example.com:9100"),
            DispatchMode = ZlinkStreamDispatchMode.Manual
        });

    connector.On<GameStateNotify>("GameStateNotify", (message, cancellationToken) =>
    {
        Render(message.Payload);
        return ValueTask.CompletedTask;
    });

    await connector.Connect.Async(cancellationToken); // 연결과 receive loop 준비를 완료한다.

    while (running)
    {
        await connector.Dispatch.Async(cancellationToken); // Manual 모드는 이 caller에서 callback을 실행한다.
    }
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


게임 loop나 UI thread에서 callback을 실행해야 하면 `Manual`을 사용한다. `Immediate`는 connector의
worker에서 callback을 실행하므로 thread affinity가 필요한 client에는 적합하지 않다.

## 6. Client send와 request

=== "C#/.NET"

    ```csharp
    await connector
        .Send(new PlayerInput(direction))
        .Async(cancellationToken); // bounded outbound queue admission까지 기다린다.

    Profile profile = await connector
        .Request(new GetProfile(playerId))
        .Async<Profile>(cancellationToken); // request sequence로 response를 찾는다.
    ```

=== "C++"

    C++ 예제는 준비 중이다.

=== "Java"

    Java 예제는 준비 중이다.

=== "Kotlin"

    Kotlin 예제는 준비 중이다.

=== "Node/TypeScript"

    Node 예제는 준비 중이다.


Connector의 기본 typed codec은 JSON이다. Packet name override, push 대기, reconnect, heartbeat와
bounded queue 설정은 Stream Connector 가이드에서 설명한다.

## 7. 관련 문서

- 이 챕터 계약의 실행 검증 예문: `13. Interface 카탈로그` 장 §5 — 검증 클래스 `StreamContracts`
- Session과 Actor binding: [Session Actor Dispatch](08-actor-session.ko.md)
- Client connector 전체 사용법: Stream Connector 가이드
- Location Store와 자동 연결: [Location](10-location.ko.md)
