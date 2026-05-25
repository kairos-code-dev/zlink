using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Actors;

public sealed class ActorContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkActor),
        typeof(IZLinkActorContext),
        typeof(IZLinkActorJoinSpotCall),
        typeof(IZLinkActorJoinEntrySpotCall),
        typeof(IZLinkActorFactory),
        typeof(IZLinkActorManager))]
    public async Task Actor_context_creates_actors_and_joins_a_spot_by_routing_id()
    {
        var spot = new RoomSpot();
        var context = new ActorContext("player-1", spot);
        var factory = new ActorFactory();
        var manager = new ActorManager(factory, context);

        var actor = await manager.GetOrCreateAsync("player-1", "player");
        var joinReply = await actor.Context
            .JoinSpot(RoutingId.Of("room-1"), new JoinRoom("room-1"))
            .SubmitAsync<JoinedRoom>();
        var entryJoin = await actor.Context
            .JoinEntrySpot(RoutingId.Of("play-node"))
            .Timeout(TimeSpan.FromSeconds(1))
            .SubmitAsync();

        actor.Configure();
        await actor.OnDisconnectedAsync(CancellationToken.None);

        Assert.Equal("player-1", actor.ActorId);
        Assert.Equal("room-1", joinReply.Reply.RoomId);
        Assert.Equal("player-1", entryJoin.ActorId);
        Assert.Equal(RoutingId.Of("play-node"), entryJoin.RemoteAddress.TargetNodeRid);
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

        public ValueTask<ZLinkActorRemoteAddress> GetRemoteAddressAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkActorRemoteAddress(
                string.Empty,
                RoutingId.Of("actor-node"),
                1));

        public async ValueTask<IZLinkActor> GetOrCreateAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            await FindAsync(actorId, cancellationToken)
                ?? await CreateAsync(actorId, actorType, cancellationToken);
    }

    private sealed class ActorContext(string actorId, IZLinkSpot spot) : IZLinkActorContext
    {
        public string ActorId { get; } = actorId;

        public string? SessionId => "session-1";

        public string? SpotName => "room";

        public RoutingId? SpotRid => RoutingId.Of("room-1");

        public bool IsJoined => true;

        public IZLinkBoundSession BoundSession { get; } = new BoundSession();

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddPacket<THandler>(string messageName)
            where THandler : class { }

        public IZLinkSpot GetSpot() => spot;

        public TSpot GetSpot<TSpot>()
            where TSpot : IZLinkSpot =>
            (TSpot)spot;

        public IZLinkActorJoinSpotCall JoinSpot<TRequest>(
            RoutingId spotRid,
            TRequest request) =>
            new JoinSpotCall(new JoinedRoom("room-1"));

        public IZLinkActorJoinEntrySpotCall JoinEntrySpot(RoutingId spotNodeRid) =>
            new JoinEntrySpotCall(new ZLinkActorJoinResult(
                actorId,
                "player",
                new ZLinkActorRemoteAddress(string.Empty, spotNodeRid, 1)));
    }

    private sealed class JoinSpotCall(object reply) : IZLinkActorJoinSpotCall
    {
        public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout) => this;

        public ValueTask<ZLinkActorJoinResult<TReply>> SubmitAsync<TReply>(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkActorJoinResult<TReply>(
                "player-1",
                "player",
                new ZLinkActorRemoteAddress(string.Empty, RoutingId.Of("room-node"), 1),
                (TReply)reply));
    }

    private sealed class JoinEntrySpotCall(ZLinkActorJoinResult result) : IZLinkActorJoinEntrySpotCall
    {
        public IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout) => this;

        public ValueTask<ZLinkActorJoinResult> SubmitAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(result);
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

    private sealed class BoundSession : IZLinkBoundSession
    {
        public IZLinkBoundSessionSendCall Send<TMessage>(TMessage message) => null!;

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
