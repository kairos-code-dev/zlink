using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Actors;

public sealed class ActorContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkActor),
        typeof(IZLinkActorContext),
        typeof(IZLinkActorJoinSpotCall),
        typeof(IZLinkActorFactory),
        typeof(IZLinkActorManager),
        typeof(IZLinkActorPlayRouteResolver))]
    public async Task Actor_context_creates_actors_and_joins_a_spot_by_routing_id()
    {
        var spot = new RoomSpot();
        var context = new ActorContext("player-1", spot);
        var factory = new ActorFactory();
        var manager = new ActorManager(factory, context);
        var routeResolver = new ActorRouteResolver();

        var actor = await manager.GetOrCreateAsync("player-1", "player");
        var route = await routeResolver.ResolvePlayRouteAsync(actor.ActorId, CancellationToken.None);
        var joinReply = await actor.Context.JoinSpotAsync<JoinRoom, JoinedRoom>(
            RoutingId.Of("room-1"),
            new JoinRoom("room-1"));

        actor.Configure();
        await actor.OnDisconnectedAsync(CancellationToken.None);

        Assert.Equal("player-1", actor.ActorId);
        Assert.Equal("play-router", route.RouterChannelId);
        Assert.Equal("room-1", joinReply.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkActorSendHandler<>),
        typeof(IZLinkActorRequestHandler<,>),
        typeof(IZLinkActorPacketHandler<,>),
        typeof(IZLinkActorRequestHandler<,,>))]
    public async Task Actor_handlers_can_be_session_scoped_or_actor_instance_scoped()
    {
        var actor = new PlayerActor("player-1", new ActorContext("player-1", new RoomSpot()));
        var sendHandler = new SessionSendHandler();
        var requestHandler = new SessionRequestHandler();
        var packetHandler = new ActorPacketHandler();
        var actorRequestHandler = new ActorRequestHandler();

        await sendHandler.HandleAsync(new PlayerMessage("hello"), null!, CancellationToken.None);
        var sessionReply = await requestHandler.HandleAsync(new PlayerRequest("state"), null!, CancellationToken.None);
        await packetHandler.HandleAsync(actor, new PlayerMessage("hello"), CancellationToken.None);
        var actorReply = await actorRequestHandler.HandleAsync(actor, new PlayerRequest("state"), CancellationToken.None);

        Assert.True(sendHandler.WasCalled);
        Assert.Equal("session:state", sessionReply.Value);
        Assert.True(packetHandler.WasCalled);
        Assert.Equal("actor:player-1:state", actorReply.Value);
    }

    private sealed record JoinRoom(string RoomId);

    private sealed record JoinedRoom(string RoomId);

    private sealed record PlayerMessage(string Value);

    private sealed record PlayerRequest(string Value);

    private sealed record PlayerReply(string Value);

    private sealed class ActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkActor>(new PlayerActor(actorId, context));
    }

    private sealed class ActorManager(IZLinkActorFactory factory, IZLinkActorContext context) : IZLinkActorManager
    {
        private readonly Dictionary<string, IZLinkActor> _actors = [];

        public async ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default)
        {
            var actor = await factory.CreateAsync(actorId, context, cancellationToken);
            _actors[actorId] = actor;
            return actor;
        }

        public ValueTask<IZLinkActor?> FindAsync(
            string actorId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_actors.GetValueOrDefault(actorId));

        public ValueTask<ZLinkActorRoute> GetRouteAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkActorRoute("play-router", RoutingId.Of("play-node"), 1));

        public async ValueTask<IZLinkActor> GetOrCreateAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            await FindAsync(actorId, cancellationToken)
                ?? await CreateAsync(actorId, actorType, cancellationToken);
    }

    private sealed class ActorRouteResolver : IZLinkActorPlayRouteResolver
    {
        public ValueTask<ZLinkActorLocationRoute> ResolvePlayRouteAsync(
            string actorId,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ZLinkActorLocationRoute(
                "play-router",
                actorId,
                RoutingId.Of("play-node"),
                RoutingId.Of("room-1"),
                ZLinkSpotKind.User));
    }

    private sealed class ActorContext(string actorId, IZLinkSpot spot) : IZLinkActorContext
    {
        public string ActorId { get; } = actorId;

        public string? SessionId => "session-1";

        public string? SpotName => "room";

        public RoutingId? SpotRid => RoutingId.Of("room-1");

        public bool IsJoined => true;

        public IZLinkSessionProxy SessionProxy { get; } = new SessionProxy();

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddPacket<THandler>(string messageName)
            where THandler : class { }

        public IZLinkSpot GetSpot() => spot;

        public TSpot GetSpot<TSpot>()
            where TSpot : IZLinkSpot =>
            (TSpot)spot;

        public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
            string spotName,
            TRequest request) =>
            new JoinSpotCall(new JoinedRoom(spotName));

        public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
            RoutingId spotRid,
            TRequest request) =>
            new JoinSpotCall(new JoinedRoom("room-1"));

        public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
            string spotName,
            TRequest request,
            CancellationToken cancellationToken = default) =>
            JoinSpot(request: request, spotName: spotName).SubmitAsync<TReply>(cancellationToken);

        public ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
            RoutingId spotRid,
            TRequest request,
            CancellationToken cancellationToken = default) =>
            JoinSpot(spotRid, request).SubmitAsync<TReply>(cancellationToken);
    }

    private sealed class JoinSpotCall(object reply) : IZLinkActorJoinSpotCall
    {
        public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply);
    }

    private sealed class PlayerActor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class RoomSpot : IZLinkSpot
    {
        public IZLinkSpotContext Context => null!;
    }

    private sealed class SessionProxy : IZLinkSessionProxy
    {
        public IZLinkSessionProxySendCall Send<TMessage>(TMessage message) => null!;

        public IZLinkSessionProxyRequestCall Request<TRequest>(TRequest request) => null!;

        public ValueTask DisconnectAsync(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class SessionSendHandler : IZLinkActorSendHandler<PlayerMessage>
    {
        public bool WasCalled { get; private set; }

        public ValueTask HandleAsync(
            PlayerMessage message,
            ZLinkActorSendContext context,
            CancellationToken cancellationToken)
        {
            WasCalled = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class SessionRequestHandler : IZLinkActorRequestHandler<PlayerRequest, PlayerReply>
    {
        public ValueTask<PlayerReply> HandleAsync(
            PlayerRequest request,
            ZLinkActorRequestContext context,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new PlayerReply($"session:{request.Value}"));
    }

    private sealed class ActorPacketHandler : IZLinkActorPacketHandler<PlayerActor, PlayerMessage>
    {
        public bool WasCalled { get; private set; }

        public ValueTask HandleAsync(
            PlayerActor actor,
            PlayerMessage message,
            CancellationToken cancellationToken)
        {
            WasCalled = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class ActorRequestHandler : IZLinkActorRequestHandler<PlayerActor, PlayerRequest, PlayerReply>
    {
        public ValueTask<PlayerReply> HandleAsync(
            PlayerActor actor,
            PlayerRequest request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new PlayerReply($"actor:{actor.ActorId}:{request.Value}"));
    }
}
