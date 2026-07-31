# 9. STREAM

> 서버의 정확한 시그니처는 [언어별 STREAM session 공개 계약](../../../common/spec/server/languages/README.ko.md)이
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

    ```cpp
    options.add_stream_node ("client-stream")
      .bind ("tcp://0.0.0.0:9100")
      .enable_actor_dispatch ()
      .register_session<play_session_t> (); // 연결마다 만들 session type을 등록한다.
    ```

=== "Java"

    ```java
    options.addStreamNode("client-stream")
        .bind("tcp://0.0.0.0:9100")
        .enableActorDispatch("play")          // Java는 Actor를 배치한 mesh 이름을 함께 준다.
        .registerSession(PlaySession.class);  // 연결마다 만들 session type을 등록한다.
    ```

=== "Kotlin"

    ```kotlin
    options.addStreamNode("client-stream")
        .bind("tcp://0.0.0.0:9100")
        .enableActorDispatch("play")            // Kotlin도 Java 표면을 그대로 쓴다.
        .registerSession(PlaySession::class.java) // 연결마다 만들 session type을 등록한다.
    ```

=== "Node/TypeScript"

    ```typescript
    builder.addStreamNode('client-stream')
      .bind('tcp://0.0.0.0:9100')
      .enableActorDispatch()
      .registerSession(PlaySessionFactory); // 연결마다 만들 session factory를 등록한다.
    ```


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

    ```cpp
    // C++ session은 packet_stream_session_t를 상속하고 callback을 override한다.
    class play_session_t : public packet_stream_session_t
    {
      public:
        task_t<void> on_connected (stream_t &stream) override
        {
            _logger.info ("connected");
            co_return;
        }

        task_t<void> on_packet (stream_t &stream,
                                const stream_dispatch_context_t &dispatch,
                                const zlink::message_t &payload) override
        {
            if (!_ping.can_handle (dispatch)) {
                // application protocol에 없는 packet을 받으면 연결을 닫는다.
                co_await stream.close ();
                co_return;
            }
            co_await _ping.handle (stream, payload);
        }

        task_t<void> on_error (stream_t &, const stream_error_t &error) override
        {
            _logger.warn (std::string ("session error: ") + error.message);
            co_return;
        }

        task_t<void> on_disconnected (stream_t &) override
        {
            _logger.info ("disconnected");
            co_return;
        }
    };
    ```

=== "Java"

    ```java
    public final class PlaySession implements ZLinkSession {
        private final ZLinkSessionContext context;
        private final Logger logger;

        @Override
        public void configure() {
            context.handlers().addHandler(PingHandler.class); // typed session packet handler를 등록한다.
        }

        @Override
        public CompletionStage<Void> onConnected() {
            logger.info("connected: {}", context.sessionId());
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionMessageContext dispatch, ZLinkMessage payload) {
            return context.handlers().tryHandle(dispatch, payload).thenCompose(handled -> handled
                ? CompletableFuture.<Void>completedFuture(null)
                // application protocol에 없는 packet을 받으면 연결을 닫는다.
                : context.close().toCompletableFuture());
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            logger.info("disconnected: {}", context.sessionId());
            return CompletableFuture.completedFuture(null);
        }
    }
    ```

=== "Kotlin"

    ```kotlin
    class PlaySession(
        private val context: ZLinkSessionContext,
        private val logger: Logger,
    ) : ZLinkSession {

        override fun configure() {
            context.handlers().addHandler(PingHandler::class.java) // typed session packet handler를 등록한다.
        }

        override suspend fun onConnected() {
            logger.info("connected: {}", context.sessionId())
        }

        override suspend fun onDispatch(dispatch: ZLinkSessionMessageContext, payload: ZLinkMessage) {
            if (context.handlers().tryHandle(dispatch, payload).await()) return
            // application protocol에 없는 packet을 받으면 연결을 닫는다.
            context.close().await()
        }

        override suspend fun onDisconnected() {
            logger.info("disconnected: {}", context.sessionId())
        }
    }
    ```

=== "Node/TypeScript"

    ```typescript
    export class PlaySession implements ZLinkSession {
      constructor(
        private readonly context: ZLinkSessionContext,
        private readonly logger: Logger
      ) {}

      configure(): void {
        this.context.handlers.addHandler(PingHandler); // typed session packet handler를 등록한다.
      }

      async onConnected(): Promise<void> {
        this.logger.log(`connected: ${this.context.sessionId}`);
      }

      async onDispatch(dispatch: ZLinkSessionMessageContext, payload: ZLinkMessage): Promise<void> {
        if (await this.context.handlers.tryHandle(dispatch, payload)) return;
        // application protocol에 없는 packet을 받으면 연결을 닫는다.
        await this.context.close();
      }

      async onDisconnected(): Promise<void> {
        this.logger.log(`disconnected: ${this.context.sessionId}`);
      }
    }
    ```


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

    ```cpp
    task_t<void> handle (stream_t &stream,
                         const stream_dispatch_context_t &dispatch,
                         const ping_t &message)
    {
        if (!dispatch.can_reply ())
            throw std::runtime_error ("Ping must be a request.");

        // 같은 request correlation으로 한 번만 reply한다.
        co_await stream.reply_packet (zlink::message_t::from_json (pong_t{message.sequence}))
          .submit ();
    }
    ```

=== "Java"

    ```java
    public CompletionStage<Void> handle(
        ZLinkSessionContext context, ZLinkSessionMessageContext dispatch, Ping message) {
        if (!dispatch.canReply()) {
            throw new IllegalStateException("Ping must be a request.");
        }

        // 같은 request correlation으로 한 번만 reply한다.
        return context.client().reply(new Pong(message.sequence())).submit();
    }
    ```

=== "Kotlin"

    ```kotlin
    suspend fun handle(
        context: ZLinkSessionContext, dispatch: ZLinkSessionMessageContext, message: Ping) {
        check(dispatch.canReply()) { "Ping must be a request." }

        // 같은 request correlation으로 한 번만 reply한다.
        context.client().reply(Pong(message.sequence)).submit().await()
    }
    ```

=== "Node/TypeScript"

    ```typescript
    async handle(
      context: ZLinkSessionContext, dispatch: ZLinkSessionMessageContext, payload: ZLinkMessage) {
      if (!dispatch.canReply) throw new Error('Ping must be a request.');

      const message = payload.decode<Ping>(Object as never);
      // 같은 request correlation으로 한 번만 reply한다.
      await context.client.reply(pong(message.sequence)).submit();
    }
    ```


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

    ```cpp
    // local transport queue admission까지 기다린다.
    co_await stream.send (server_notice_t{"maintenance"})
      .metadata ("severity", "info")
      .compress ()
      .submit ();
    ```

=== "Java"

    ```java
    // local transport queue admission까지 기다린다.
    context.client()
        .send(new ServerNotice("maintenance"))
        .metadata("severity", "info")
        .compress()
        .submit();
    ```

=== "Kotlin"

    ```kotlin
    // local transport queue admission까지 기다린다.
    context.client()
        .send(ServerNotice("maintenance"))
        .metadata("severity", "info")
        .compress()
        .submit()
        .await()
    ```

=== "Node/TypeScript"

    ```typescript
    // local transport queue admission까지 기다린다.
    await context.client
      .send(serverNotice('maintenance'))
      .metadata('severity', 'info')
      .compress()
      .submit();
    ```


## 4. Actor dispatch

인증 뒤 Actor를 session에 bind하고, session 전용 handler가 처리하지 않은 `ZLinkMessage`를
session actor의 relay 호출로 넘길 수 있다. 상세 흐름은
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

    ```cpp
    zlink::stream_connector::connector_options_t connector_options;
    connector_options.endpoint = "tcp://game.example.com:9100";
    connector_options.dispatch_mode = zlink::stream_connector::dispatch_mode_t::manual;
    auto connector = zlink::stream_connector::connector_factory_t::create (connector_options);

    connector.on<game_state_notify_t> ("GameStateNotify",
                                       [] (const auto &message) { render (message.payload ()); });

    co_await connector.connect ().submit (); // 연결과 receive loop 준비를 완료한다.

    while (running) {
        co_await connector.dispatch ().submit (); // manual 모드는 이 caller에서 callback을 실행한다.
    }
    ```

=== "Java"

    ```java
    ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
        new ZLinkStreamConnectorOptions(
            URI.create("tcp://game.example.com:9100"),
            ZLinkStreamDispatchMode.MANUAL));

    connector.on(GameStateNotify.class, message -> {
        render(message.payload());
        return CompletableFuture.completedFuture(null);
    });

    connector.connect().submit().toCompletableFuture().join(); // 연결과 receive loop 준비를 완료한다.

    while (running) {
        // MANUAL 모드는 이 caller에서 callback을 실행한다.
        connector.dispatch().submit().toCompletableFuture().join();
    }
    ```

=== "Kotlin"

    ```kotlin
    val connector = ZLinkStreamConnectorFactory.create(
        ZLinkStreamConnectorOptions(
            URI.create("tcp://game.example.com:9100"),
            ZLinkStreamDispatchMode.MANUAL))

    connector.on(GameStateNotify::class.java) { message ->
        render(message.payload())
        CompletableFuture.completedFuture(null)
    }

    connector.connect().submit().await() // 연결과 receive loop 준비를 완료한다.

    while (running) {
        // MANUAL 모드는 이 caller에서 callback을 실행한다.
        connector.dispatch().submit().await()
    }
    ```

=== "Node/TypeScript"

    ```typescript
    const client = zlinkStreamConnectorFactory.create({
      endpoint: 'tcp://game.example.com:9100',
      dispatchMode: ZlinkStreamDispatchMode.Manual
    });

    client.on(GameStateNotify, (message) => {
      render(message.payload);
    });

    await client.connect(); // 연결과 receive loop 준비를 완료한다.

    while (running) {
      await client.dispatch(); // Manual 모드는 이 caller에서 callback을 실행한다.
    }
    ```


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

    ```cpp
    // bounded outbound queue admission까지 기다린다.
    co_await connector.send (player_input_t{direction}).submit ();

    // request sequence로 response를 찾는다.
    auto profile = co_await connector.request (get_profile_t{player_id}).submit<profile_t> ();
    ```

=== "Java"

    ```java
    // bounded outbound queue admission까지 기다린다.
    connector.send(new PlayerInput(direction)).submit().toCompletableFuture().join();

    // request sequence로 response를 찾는다.
    Profile profile = connector
        .request(new GetProfile(playerId))
        .submit(Profile.class)
        .toCompletableFuture().join();
    ```

=== "Kotlin"

    ```kotlin
    // bounded outbound queue admission까지 기다린다.
    connector.send(PlayerInput(direction)).submit().await()

    // request sequence로 response를 찾는다.
    val profile = connector.request(GetProfile(playerId)).submit(Profile::class.java).await()
    ```

=== "Node/TypeScript"

    ```typescript
    // bounded outbound queue admission까지 기다린다.
    await client.send(playerInput(direction)).submit();

    // request sequence로 response를 찾는다.
    const profile = await client.request(getProfile(playerId)).submit<Profile>();
    ```


Connector의 기본 typed codec은 JSON이다. Packet name override, push 대기, reconnect, heartbeat와
bounded queue 설정은 Stream Connector 가이드에서 설명한다.

## 7. 관련 문서

- 이 챕터 계약의 실행 검증 예문: `13. Interface 카탈로그` 장 §5 — 검증 클래스 `StreamContracts`
- Session과 Actor binding: [Session Actor Dispatch](08-actor-session.ko.md)
- Client connector 전체 사용법: Stream Connector 가이드
- Location Store와 자동 연결: [Location](10-location.ko.md)
