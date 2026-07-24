using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Actors;

public sealed class ActorContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkActor),
        typeof(IZLinkActorContext),
        typeof(IZLinkActorDeferredJoinCall),
        typeof(ZLinkActorJoinCompletion),
        typeof(IZLinkActorJoinSpotCall),
        typeof(IZLinkActorJoinEntrySpotCall),
        typeof(IZLinkActorClient),
        typeof(IZLinkActorSendCall),
        typeof(IZLinkActorRequestCall),
        typeof(IZLinkActorFactory),
        typeof(IZLinkActorManager),
        typeof(IZLinkActorCreateCall),
        typeof(IZLinkActorGetOrCreateCall),
        typeof(ZLinkActorCreateResult))]
    public async Task Actor_context_creates_actors_and_joins_a_spot_by_spot_id()
    {
        var spot = new RoomSpot();
        var context = new ActorContext("player-1");
        var factory = new ActorFactory();
        var manager = new ActorManager(factory, context);
        var actorClient = new ActorClient();

        var actor = new PlayerActor("player-1", context);
        var actorRef = (await manager.GetOrCreate("player-1", "player")
            .InMesh("actors")
            .Request(new JoinRoom("room-1"))
            .Timeout(TimeSpan.FromSeconds(1))
            .Async()) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Actor creation was rejected.")
        };
        var foundActorRef = await manager.FindAsync("player-1");
        var snapshot = ActorRefSnapshot.From(actorRef);
        actor.Context
            .JoinSpot("room-1", new JoinRoom("room-1"))
            .Defer();
        actor.Context
            .JoinEntrySpot(ZLinkMessage.Empty)
            .Timeout(TimeSpan.FromSeconds(1))
            .Defer();
        await actorClient.SendToActor("actors", actorRef, new JoinRoom("room-1"))
            .Async();
        var actorReply = await actorClient
            .RequestToActor("actors", actorRef, new JoinRoom("room-1"))
            .Timeout(TimeSpan.FromSeconds(1))
            .Async<JoinedRoom>();
        var yieldedActorReply = await actorClient
            .RequestToActor("actors", actorRef, new JoinRoom("room-1"))
            .Timeout(TimeSpan.FromSeconds(1))
            .Yield<JoinedRoom>();

        Assert.Equal("player-1", actorRef.ActorId);
        Assert.Equal(actorRef, foundActorRef);
        Assert.Equal(actorRef, snapshot.ToActorRef());
        Assert.Equal("player-1", actor.ActorId);
        Assert.Equal("room-1", actorReply.RoomId);
        Assert.Equal("room-1", yieldedActorReply.RoomId);
    }

    private sealed class ActorClient : IZLinkActorClient
    {
        public IZLinkActorSendCall SendToActor<TMessage>(
            string meshName,
            ActorRef actor,
            TMessage message)
        {
            _ = meshName;
            _ = actor;
            _ = message;
            return new ActorSendCall();
        }

        public IZLinkActorRequestCall RequestToActor<TRequest>(
            string meshName,
            ActorRef actor,
            TRequest request)
        {
            _ = meshName;
            _ = actor;
            _ = request;
            return new ActorRequestCall();
        }
    }

    private sealed class ActorSendCall : IZLinkActorSendCall
    {
        public IZLinkActorSendCall Metadata(string key, string value) => this;

        public IZLinkActorSendCall Metadata(ZLinkMessageMetadata metadata) => this;

        public ValueTask Async(
            CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;
    }

    private sealed class ActorRequestCall : IZLinkActorRequestCall
    {
        public IZLinkActorRequestCall Metadata(string key, string value) => this;

        public IZLinkActorRequestCall Metadata(ZLinkMessageMetadata metadata) => this;

        public IZLinkActorRequestCall Timeout(TimeSpan timeout)
        {
            _ = timeout;
            return this;
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            object reply = new JoinedRoom("room-1");
            return ValueTask.FromResult((TReply)reply);
        }

        public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
        {
            return Async<TReply>(cancellationToken);
        }
    }

    private sealed record JoinRoom(string RoomId);

    private sealed record JoinedRoom(string RoomId);

    private sealed class ActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<IZLinkActor>(new PlayerActor(actorId, context));
        }
    }

    private sealed class ActorManager(IZLinkActorFactory factory, IZLinkActorContext context) : IZLinkActorManager
    {
        private readonly Dictionary<string, ActorRef> _actors = [];

        public IZLinkActorCreateCall Create(string actorId, string actorType) =>
            new ActorCreateCall(this, actorId, actorType, getOrCreate: false);

        public IZLinkActorGetOrCreateCall GetOrCreate(string actorId, string actorType) =>
            new ActorCreateCall(this, actorId, actorType, getOrCreate: true);

        public ValueTask<ActorRef?> FindAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<ActorRef?>(
                _actors.TryGetValue(actorId, out var actorRef)
                    ? actorRef
                    : null);
        }

        public ValueTask<SpotRef?> FindSpotAsync(
            string actorId,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<SpotRef?>(null);

        public ValueTask<bool> DestroyAsync(
            ActorRef actor,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_actors.Remove(actor.ActorId));

        private async ValueTask<ZLinkActorCreateResult> SubmitAsync(
            string actorId,
            string actorType,
            bool getOrCreate,
            CancellationToken cancellationToken)
        {
            if (getOrCreate && await FindAsync(actorId, cancellationToken) is { } existing)
                return new ZLinkActorCreateResult.Existing(existing);

            _ = actorType;
            var actor = await factory.CreateAsync(actorId, context, cancellationToken);
            var actorRef = new ActorRef(RoutingId.From("actor-node"), actor.ActorId, 1);
            _actors[actorId] = actorRef;
            return new ZLinkActorCreateResult.Created(actorRef, null);
        }

        private sealed class ActorCreateCall(
            ActorManager manager,
            string actorId,
            string actorType,
            bool getOrCreate) : IZLinkActorCreateCall, IZLinkActorGetOrCreateCall
        {
            public IZLinkActorCreateCall InMesh(string meshName)
            {
                _ = meshName;
                return this;
            }

            IZLinkActorGetOrCreateCall IZLinkActorGetOrCreateCall.InMesh(string meshName)
            {
                _ = meshName;
                return this;
            }

            public IZLinkActorCreateCall Request(ZLinkMessage request)
            {
                _ = request;
                return this;
            }

            IZLinkActorGetOrCreateCall IZLinkActorGetOrCreateCall.Request(ZLinkMessage request)
            {
                _ = request;
                return this;
            }

            public IZLinkActorCreateCall Request<TRequest>(TRequest request)
            {
                _ = request;
                return this;
            }

            IZLinkActorGetOrCreateCall IZLinkActorGetOrCreateCall.Request<TRequest>(TRequest request)
            {
                _ = request;
                return this;
            }

            public IZLinkActorCreateCall Timeout(TimeSpan timeout)
            {
                _ = timeout;
                return this;
            }

            IZLinkActorGetOrCreateCall IZLinkActorGetOrCreateCall.Timeout(TimeSpan timeout)
            {
                _ = timeout;
                return this;
            }

            public ValueTask<ZLinkActorCreateResult> Async(
                CancellationToken cancellationToken = default) =>
                manager.SubmitAsync(actorId, actorType, getOrCreate, cancellationToken);
        }
    }

    private sealed class ActorContext(string actorId) : IZLinkActorContext
    {
        public string ActorId => actorId;

        public ulong ObjectGeneration => 1;

        public string MeshName => "play";

        public string? SpotId => "room-1";

        public IZLinkBoundSession BoundSession { get; } = new BoundSession();

        public IZLinkActorJoinSpotCall JoinSpot(
            string spotId,
            ZLinkMessage request)
        {
            return new JoinSpotCall(ZLinkMessage.From(new JoinedRoom("room-1")));
        }

        public IZLinkActorJoinEntrySpotCall JoinEntrySpot(ZLinkMessage request)
        {
            return new JoinEntrySpotCall(
                new ActorRef(RoutingId.From("entry-node"), actorId, 1),
                request);
        }
    }

    private sealed class JoinSpotCall(ZLinkMessage reply) : IZLinkActorJoinSpotCall
    {
        public void Defer()
        {
        }

        public IZLinkActorJoinSpotCall Timeout(TimeSpan timeout)
        {
            return this;
        }

    }

    private sealed class JoinEntrySpotCall(ActorRef result, ZLinkMessage reply) : IZLinkActorJoinEntrySpotCall
    {
        public void Defer()
        {
        }

        public IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout)
        {
            return this;
        }

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
        public IZLinkBoundSessionSendCall Send<TMessage>(TMessage message)
        {
            return null!;
        }

        public ValueTask DisconnectAsync(CancellationToken cancellationToken = default)
        {
            return ValueTask.CompletedTask;
        }
    }
}
