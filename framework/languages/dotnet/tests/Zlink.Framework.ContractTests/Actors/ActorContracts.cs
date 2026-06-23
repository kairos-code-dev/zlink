using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Messaging;
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
            .JoinSpot(RoutingId.From("room-1"), new JoinRoom("room-1"))
            .Async();
        var entryJoin = await actor.Context
            .JoinEntrySpot(RoutingId.From("play-node"), ZLinkMessage.Empty)
            .Timeout(TimeSpan.FromSeconds(1))
            .Async();

        actor.Configure();

        Assert.Equal("player-1", actor.ActorId);
        Assert.True(joinReply.Accepted);
        Assert.Equal("room-1", joinReply.Reply.Decode<JoinedRoom>().RoomId);
        Assert.True(entryJoin.Accepted);
        Assert.Equal("player-1", entryJoin.Actor.ActorId);
        Assert.Equal(RoutingId.From("play-node"), entryJoin.Actor.NodeRid);
    }

    private sealed record JoinRoom(string RoomId);

    private sealed record JoinedRoom(string RoomId);

    private static Message Encode<T>(T value) => value.ToJson();

    private static T Decode<T>(Message message) => message.FromJson<T>();

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

        public async ValueTask<IZLinkActor> GetOrCreateAsync(
            string actorId,
            string actorType,
            CancellationToken cancellationToken = default) =>
            await FindAsync(actorId, cancellationToken)
                ?? await CreateAsync(actorId, actorType, cancellationToken);
    }

    private sealed class ActorContext(string actorId, IZLinkSpot spot) : IZLinkActorContext
    {
        public RoutingId? SpotRid => RoutingId.From("room-1");

        public bool IsJoined => true;

        public IZLinkBoundSession BoundSession { get; } = new BoundSession();

        public IZLinkSpot GetSpot() => spot;

        public TSpot GetSpot<TSpot>()
            where TSpot : IZLinkSpot =>
            (TSpot)spot;

        public IZLinkActorJoinSpotCall JoinSpot(
            RoutingId spotRid,
            ZLinkMessage request) =>
            new JoinSpotCall(ZLinkMessage.From(new JoinedRoom("room-1")));

        public IZLinkActorJoinEntrySpotCall JoinEntrySpot(RoutingId spotNodeRid, ZLinkMessage request) =>
            new JoinEntrySpotCall(new ActorRef(spotNodeRid, actorId, 1), request);
    }

    private sealed class JoinSpotCall(ZLinkMessage reply) : IZLinkActorJoinSpotCall
    {
        public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout) => this;

        public ValueTask<ZLinkActorJoinResult> Async(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkActorJoinResult(
                true,
                new ActorRef(RoutingId.From("room-node"), "player-1", 1),
                reply));
    }

    private sealed class JoinEntrySpotCall(ActorRef result, ZLinkMessage reply) : IZLinkActorJoinEntrySpotCall
    {
        public IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout) => this;

        public ValueTask<ZLinkActorJoinResult> Async(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkActorJoinResult(true, result, reply));
    }

    private sealed class PlayerActor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
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

}
