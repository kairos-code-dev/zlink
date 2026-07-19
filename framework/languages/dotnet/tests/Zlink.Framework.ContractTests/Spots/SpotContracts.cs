using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Workers;
using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Spots;

public sealed class SpotContracts
{
    [Fact]
    public void Actor_join_and_transfer_public_contract_matches_common_spec()
    {
        var lifecycle = typeof(IZLinkSpotActorLifecycle<>).MakeGenericType(typeof(PlayerActor));
        var join = lifecycle.GetMethod(nameof(IZLinkSpot<PlayerActor>.OnActorJoinAsync));
        Assert.NotNull(join);
        Assert.True(join.IsAbstract);
        Assert.Collection(
            join.GetParameters(),
            actorId => Assert.Equal(typeof(string), actorId.ParameterType),
            request => Assert.Equal(typeof(ZLinkMessage), request.ParameterType),
            cancellation => Assert.Equal(typeof(CancellationToken), cancellation.ParameterType));

        Assert.True(lifecycle.GetMethod(nameof(IZLinkSpot<PlayerActor>.OnJoinedActorAsync))!.IsAbstract);
        Assert.True(lifecycle.GetMethod(nameof(IZLinkSpot<PlayerActor>.OnLeaveActorAsync))!.IsAbstract);
        Assert.Null(typeof(IZLinkSpot).Assembly.GetType("Zlink.Framework.Contracts.Spots.ZLinkActorJoinAdmission"));
        Assert.Null(typeof(IZLinkMeshNodeBuilder).GetMethod("AddStatelessActorTransfer"));
        Assert.NotNull(typeof(IZLinkMeshNodeBuilder).GetMethod("AddActorTransferAdapter"));
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpot),
        typeof(IZLinkSpot<>),
        typeof(IZLinkSpotActorLifecycle<>),
        typeof(IZLinkActorTransferAdapter<>),
        typeof(IZLinkEntrySpot),
        typeof(IZLinkEntrySpot<>),
        typeof(IZLinkActorHandlerRegistry),
        typeof(IZLinkSpotHandlerRegistry),
        typeof(IZLinkSpotOutbound),
        typeof(IZLinkWorkerCall<>),
        typeof(IZLinkWorkerOptions),
        typeof(IZLinkSpotCommonContext),
        typeof(IZLinkSpotContext),
        typeof(IZLinkEntrySpotContext),
        typeof(IZLinkTimer))]
    public async Task Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages()
    {
        var context = new SpotContext(RoutingId.From("room-1"));
        IZLinkSpot spot = new RoomSpot(context);
        var entryContext = new EntrySpotContext(RoutingId.From("entry"));
        IZLinkEntrySpot<PlayerActor> entrySpot = new EntrySpot(entryContext);
        var actor = new PlayerActor("player-1");

        spot.Configure();
        entrySpot.Configure();
        context.Handlers.AddHandler<RoomPacketHandler>();
        context.Handlers.AddHandler<RoomPacketHandler>("room.packet");
        context.Handlers.AddPacket<RoomPacketHandler>();
        context.Handlers.AddSubscribe<RoomEventHandler>("room.events");
        context.Handlers.AddActorPacket<PlayerActorPacketHandler, PlayerActor>();
        context.Handlers.AddHandler<PlayerActorPacketHandler>("actor.packet");
        context.Handlers.AddHandler<PlayerActorRequestHandler>("actor.request");
        await context.LeaveActorAsync(actor);
        await entryContext.DestroyActorAsync(actor);
        await context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
        context.Outbound.SendToSpot(null!, new RoomEvent("opened")).TrySubmit();
        await context.Outbound.RequestToSpot(null!, new JoinRoom("room-2")).Async<JoinedRoom>();
        await context.Outbound.RequestToSpot(null!, new JoinRoom("room-2")).Yield<JoinedRoom>();
        context.Outbound.Publish("room.events", new RoomEvent("opened")).TrySubmit();
        context.Outbound.SendToChannel("api", new RoomEvent("opened")).TrySubmit();
        await context.Outbound.RequestToChannel("api", new JoinRoom("room-1")).Async<JoinedRoom>();
        await context.Outbound.RequestToChannel("api", new JoinRoom("room-1")).Yield<JoinedRoom>();

        await spot.OnCreateAsync(ZLinkMessage.Empty, CancellationToken.None);
        await spot.OnInitializeAsync(CancellationToken.None);
        await spot.OnClosingAsync(CancellationToken.None);
        await entrySpot.OnInitializeAsync(CancellationToken.None);
        await entrySpot.OnClosingAsync(CancellationToken.None);

        Assert.Equal(["player-1"], context.LeftActors);
        Assert.Equal(["player-1"], entryContext.DestroyedActors);
        Assert.Contains("heartbeat", context.Timers);
        Assert.Equal(RoutingId.From("entry"), entryContext.SpotRid);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotCommonContext),
        typeof(IZLinkSpotContext),
        typeof(IZLinkEntrySpotContext))]
    public void Actor_destroy_is_entry_spot_context_only()
    {
        Assert.Null(typeof(IZLinkSpotContext).GetMethod("DestroyActorAsync"));
        Assert.Null(typeof(IZLinkEntrySpotContext).GetMethod("destroyActor"));

        var destroy = typeof(IZLinkEntrySpotContext).GetMethod("DestroyActorAsync");
        Assert.NotNull(destroy);
        Assert.Equal(typeof(ValueTask), destroy.ReturnType);
        Assert.Collection(
            destroy.GetParameters(),
            actor => Assert.Equal(typeof(IZLinkActor), actor.ParameterType),
            cancellation => Assert.Equal(typeof(CancellationToken), cancellation.ParameterType));
    }

    [Fact]
    [ContractExample(typeof(IZLinkSpotOutbound))]
    public async Task Spot_outbound_context_exposes_all_routed_channel_and_publish_methods()
    {
        IZLinkSpotOutbound spotOutbound = new SpotContext(RoutingId.From("room-1"));
        IZLinkSpotOutbound entryOutbound = new EntrySpotContext(RoutingId.From("entry"));

        spotOutbound.SendToSpot(null!, new RoomEvent("spot-send")).TrySubmit();
        await spotOutbound.RequestToSpot(null!, new JoinRoom("room-2")).Async<JoinedRoom>();
        spotOutbound.Publish("room.events", new RoomEvent("spot-publish")).TrySubmit();
        spotOutbound.SendToChannel("api", new RoomEvent("spot-channel-send")).TrySubmit();
        await spotOutbound.RequestToChannel("api", new JoinRoom("room-1")).Async<JoinedRoom>();

        entryOutbound.SendToSpot(null!, new RoomEvent("entry-send")).TrySubmit();
        await entryOutbound.RequestToSpot(null!, new JoinRoom("room-2")).Async<JoinedRoom>();
        entryOutbound.Publish("room.events", new RoomEvent("entry-publish")).TrySubmit();
        entryOutbound.SendToChannel("api", new RoomEvent("entry-channel-send")).TrySubmit();
        await entryOutbound.RequestToChannel("api", new JoinRoom("entry")).Async<JoinedRoom>();
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotManager),
        typeof(IZLinkSpotClient),
        typeof(IZLinkSpotOutbound),
        typeof(IZLinkSpotPublisherClient))]
    public async Task Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels()
    {
        var manager = new SpotManager();
        var created = await manager.GetOrCreateAsync<RoomSpot>(
            RoutingId.From("room-1"),
            ZLinkMessage.Empty);
        IZLinkSpotClient localClient = new SpotOutbound();
        localClient.SendToSpot(null!, new RoomEvent("opened")).TrySubmit();
        var reply = await localClient.RequestToSpot(null!, new JoinRoom("room-1")).Async<JoinedRoom>();

        IZLinkSpotPublisherClient publisher = new SpotPublisherClient();
        publisher.PublishSpot("play-events", "room.events", new RoomEvent("opened")).TrySubmit();

        Assert.Equal(ZLinkSpotCreateState.Created, created.State);
        Assert.Equal("room-1", reply.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotPacketHandler<,>),
        typeof(IZLinkSpotRequestHandler<,,>),
        typeof(IZLinkSpotSubscriptionHandler<,>),
        typeof(IZLinkSpotTimerHandler<>),
        typeof(IZLinkSpotActorSendHandler<,,>),
        typeof(IZLinkSpotActorRequestHandler<,,,>),
        typeof(IZLinkEntrySpotActorSendHandler<,,>),
        typeof(IZLinkEntrySpotActorRequestHandler<,,,>))]
    public async Task Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it()
    {
        var room = new RoomSpot(new SpotContext(RoutingId.From("room-1")));
        var entry = new EntrySpot(new EntrySpotContext(RoutingId.From("entry")));
        var actor = new PlayerActor("player-1");

        await new RoomPacketHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        var roomReply =
            await new RoomRequestHandler().HandleAsync(room, new JoinRoom("room-1"), CancellationToken.None);
        await new RoomEventHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        await new RoomTimerHandler().HandleAsync(room, TimerTick(), CancellationToken.None);
        var joinReply =
            await room.OnActorJoinAsync(Admission(actor), ZLinkMessage.From(new JoinRoom("room-1")), CancellationToken.None);
        await new PlayerActorSendHandler().HandleAsync(room, actor, null!, new RoomEvent("opened"),
            CancellationToken.None);
        var actorReply =
            await new PlayerActorRequestHandler().HandleAsync(room, actor, null!, new JoinRoom("room-1"),
                CancellationToken.None);
        await room.OnJoinedActorAsync(actor, CancellationToken.None);
        await room.OnLeaveActorAsync(actor, CancellationToken.None);
        await room.OnDisconnectActorAsync(actor, CancellationToken.None);
        await new EntryActorSendHandler().HandleAsync(entry, actor, null!, new RoomEvent("opened"),
            CancellationToken.None);
        var entryReply = await new EntryActorRequestHandler().HandleAsync(entry, actor, null!, new JoinRoom("room-1"),
            CancellationToken.None);
        await ((IZLinkEntrySpot<PlayerActor>)entry).OnCreateActorAsync(actor, ZLinkMessage.Empty,
            CancellationToken.None);
        await entry.OnJoinedActorAsync(actor, CancellationToken.None);
        await entry.OnLeaveActorAsync(actor, CancellationToken.None);
        await entry.OnDisconnectActorAsync(actor, CancellationToken.None);

        Assert.Equal("room-1", roomReply.RoomId);
        Assert.True(joinReply.Accepted);
        Assert.Equal("room-1", joinReply.Reply!.Decode<JoinedRoom>().RoomId);
        Assert.Equal("actor:room-1", actorReply.RoomId);
        Assert.Equal("entry:room-1", entryReply.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotActorSendHandler<,,>),
        typeof(IZLinkSpotActorRequestHandler<,,,>),
        typeof(IZLinkEntrySpotActorSendHandler<,,>),
        typeof(IZLinkEntrySpotActorRequestHandler<,,,>))]
    public void Spot_actor_handlers_receive_context_before_payload()
    {
        AssertHandlerParameters(
            typeof(IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>),
            typeof(RoomSpot),
            typeof(PlayerActor),
            typeof(ZLinkSpotActorSendContext),
            typeof(RoomEvent),
            typeof(CancellationToken));
        AssertHandlerParameters(
            typeof(IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>),
            typeof(RoomSpot),
            typeof(PlayerActor),
            typeof(ZLinkSpotActorRequestContext),
            typeof(JoinRoom),
            typeof(CancellationToken));
        AssertHandlerParameters(
            typeof(IZLinkEntrySpotActorSendHandler<EntrySpot, PlayerActor, RoomEvent>),
            typeof(EntrySpot),
            typeof(PlayerActor),
            typeof(ZLinkSpotActorSendContext),
            typeof(RoomEvent),
            typeof(CancellationToken));
        AssertHandlerParameters(
            typeof(IZLinkEntrySpotActorRequestHandler<EntrySpot, PlayerActor, JoinRoom, JoinedRoom>),
            typeof(EntrySpot),
            typeof(PlayerActor),
            typeof(ZLinkSpotActorRequestContext),
            typeof(JoinRoom),
            typeof(CancellationToken));

        var reply = new ZLinkSpotActorReplyOptions();
        Assert.Same(reply, reply.Metadata("reply-key", "reply-value"));
        Assert.Same(reply, reply.Compress());
    }

    [Fact]
    public async Task Spot_actor_join_callback_uses_framework_message_and_explicit_acceptance()
    {
        var room = new RoomSpot(new SpotContext(RoutingId.From("room-1")));
        var actor = new PlayerActor("player-1");
        var request = ZLinkMessage.From(new JoinRoom("room-1"));

        var accepted = await room.OnActorJoinAsync(Admission(actor), request, CancellationToken.None);
        var rejected = ZLinkSpotActorJoinResult.Reject();

        Assert.True(accepted.Accepted);
        Assert.NotNull(accepted.Reply);
        Assert.Equal("room-1", accepted.Reply!.Decode<JoinedRoom>().RoomId);
        Assert.False(rejected.Accepted);
        Assert.Null(rejected.Reply);
    }

    [Fact]
    public void Spot_actor_contexts_expose_only_dispatch_metadata()
    {
        AssertContextProperties<ZLinkSpotActorSendContext>(
            nameof(IZLinkHandlerContext.ChannelName),
            nameof(IZLinkHandlerContext.ConnectionAborted),
            nameof(IZLinkHandlerContext.ContentType),
            nameof(IZLinkHandlerContext.MeshName),
            nameof(ZLinkSpotActorSendContext.Metadata),
            nameof(IZLinkHandlerContext.PacketName));

        AssertContextProperties<ZLinkSpotActorRequestContext>(
            nameof(IZLinkHandlerContext.ChannelName),
            nameof(IZLinkHandlerContext.ConnectionAborted),
            nameof(IZLinkHandlerContext.ContentType),
            nameof(IZLinkHandlerContext.MeshName),
            nameof(ZLinkSpotActorRequestContext.Metadata),
            nameof(IZLinkHandlerContext.PacketName),
            nameof(ZLinkSpotActorRequestContext.Reply));

        Assert.Null(typeof(ZLinkSpotActorSendContext).GetProperty("ActorId"));
        Assert.Null(typeof(ZLinkSpotActorSendContext).GetProperty("BoundSession"));
        Assert.Null(typeof(ZLinkSpotActorRequestContext).GetProperty("ActorId"));
        Assert.Null(typeof(ZLinkSpotActorRequestContext).GetProperty("BoundSession"));
        Assert.Null(typeof(ZLinkSpotActorRequestContext).GetProperty("Deadline"));
        Assert.Null(typeof(ZLinkSpotActorRequestContext).GetProperty("CorrelationId"));
    }

    private static void AssertHandlerParameters(Type handlerType, params Type[] expected)
    {
        var actual = handlerType.GetMethod("HandleAsync")!
            .GetParameters()
            .Select(static parameter => parameter.ParameterType)
            .ToArray();

        Assert.Equal(expected, actual);
    }

    private static void AssertContextProperties<TContext>(params string[] expected)
    {
        var actual = typeof(TContext)
            .GetProperties()
            .Select(static property => property.Name)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(expected.Order(StringComparer.Ordinal).ToArray(), actual);
    }

    private static ZLinkTimerTick TimerTick()
    {
        return new ZLinkTimerTick(
            "heartbeat",
            1,
            1,
            TimeSpan.FromSeconds(1),
            DateTimeOffset.UtcNow,
            DateTimeOffset.UtcNow,
            TimeSpan.FromSeconds(1),
            TimeSpan.FromSeconds(1),
            TimeSpan.Zero,
            0);
    }

    private static string Admission(PlayerActor actor) => actor.ActorId;

    private sealed record JoinRoom(string RoomId);

    private sealed record JoinedRoom(string RoomId);

    private sealed record RoomEvent(string State);

    private sealed class RoomSpot(IZLinkSpotContext context) : IZLinkSpot<PlayerActor>
    {
        public IZLinkSpotContext Context { get; } = context;

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = cancellationToken;
            var join = request.Decode<JoinRoom>();
            return ValueTask.FromResult(
                ZLinkSpotActorJoinResult.Accept(new JoinedRoom(join.RoomId)));
        }

        public ValueTask OnJoinedActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class EntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<PlayerActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            _ = cancellationToken;
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
        }

        public ValueTask OnJoinedActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnDisconnectActorAsync(
            PlayerActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class PlayerActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => null!;
    }

    private sealed class SpotContext(RoutingId spotRid) :
        IZLinkSpotContext,
        IZLinkSpotHandlerRegistry,
        IZLinkSpotOutbound
    {
        public List<string> JoinedActors { get; } = [];

        public List<string> LeftActors { get; } = [];

        public List<string> Timers { get; } = [];
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.From("spot-node");

        public IZLinkSpotHandlerRegistry Handlers => this;

        public IZLinkSpotOutbound Outbound => this;

        public ValueTask LeaveActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default)
        {
            LeftActors.Add(actor.ActorId);
            return ValueTask.CompletedTask;
        }

        public ValueTask<bool> CloseAsync(CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult(true);
        }

        public ValueTask<IZLinkTimer> AddTimer<THandler>(
            string name,
            TimeSpan period,
            ZLinkTimerOptions? options = null,
            CancellationToken cancellationToken = default)
            where THandler : class
        {
            Timers.Add(name);
            return ValueTask.FromResult<IZLinkTimer>(new Timer());
        }

        public IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
            Func<CancellationToken, TResult> work)
        {
            return new WorkerCall<TResult>(work);
        }

        public IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
            Func<CancellationToken, ValueTask<TResult>> work)
        {
            return new IoWorkerCall<TResult>(work);
        }

        public void AddHandler<THandler>()
            where THandler : class
        {
        }

        public void AddHandler<THandler>(string packetName)
            where THandler : class
        {
        }

        public void AddActorPacket<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddActorPacket<THandler, TActor>(string packetName)
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddPacket<THandler>()
            where THandler : class
        {
        }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class
        {
        }

        public IZLinkSendCall SendToSpot<TMessage>(SpotHandle address, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle address, TRequest request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
        {
            return new PublishCall();
        }

        public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }
    }

    private sealed class EntrySpotContext(RoutingId spotRid) :
        IZLinkEntrySpotContext,
        IZLinkSpotHandlerRegistry,
        IZLinkSpotOutbound
    {
        public List<string> DestroyedActors { get; } = [];
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.From("spot-node");

        public IZLinkSpotHandlerRegistry Handlers => this;

        public IZLinkSpotOutbound Outbound => this;

        public ValueTask DestroyActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default)
        {
            DestroyedActors.Add(actor.ActorId);
            return ValueTask.CompletedTask;
        }

        public ValueTask<IZLinkTimer> AddTimer<THandler>(
            string name,
            TimeSpan period,
            ZLinkTimerOptions? options = null,
            CancellationToken cancellationToken = default)
            where THandler : class
        {
            return ValueTask.FromResult<IZLinkTimer>(new Timer());
        }

        public IZLinkWorkerCall<TResult> RunCpuWorker<TResult>(
            Func<CancellationToken, TResult> work)
        {
            return new WorkerCall<TResult>(work);
        }

        public IZLinkWorkerCall<TResult> RunIoWorker<TResult>(
            Func<CancellationToken, ValueTask<TResult>> work)
        {
            return new IoWorkerCall<TResult>(work);
        }

        public void AddHandler<THandler>()
            where THandler : class
        {
        }

        public void AddHandler<THandler>(string packetName)
            where THandler : class
        {
        }

        public void AddActorPacket<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddActorPacket<THandler, TActor>(string packetName)
            where THandler : class
            where TActor : IZLinkActor
        {
        }

        public void AddPacket<THandler>()
            where THandler : class
        {
        }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class
        {
        }

        public IZLinkSendCall SendToSpot<TMessage>(SpotHandle address, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle address, TRequest request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
        {
            return new PublishCall();
        }

        public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToChannel<TRequest>(string channelName, TRequest request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }
    }

    private sealed class WorkerCall<TResult>(Func<CancellationToken, TResult> work) : IZLinkWorkerCall<TResult>
    {
        public void Submit(CancellationToken cancellationToken = default)
        {
            _ = work(cancellationToken);
        }

        public IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout)
        {
            return this;
        }

        public ValueTask<TResult> Async(CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult(work(cancellationToken));
        }

        public ValueTask<TResult> Yield(CancellationToken cancellationToken = default)
        {
            return Async(cancellationToken);
        }

    }

    private sealed class IoWorkerCall<TResult>(
        Func<CancellationToken, ValueTask<TResult>> work) : IZLinkWorkerCall<TResult>
    {
        public void Submit(CancellationToken cancellationToken = default)
        {
            _ = work(cancellationToken);
        }

        public IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout)
        {
            return this;
        }

        public ValueTask<TResult> Async(CancellationToken cancellationToken = default)
        {
            return work(cancellationToken);
        }

        public ValueTask<TResult> Yield(CancellationToken cancellationToken = default)
        {
            return work(cancellationToken);
        }
    }

    private sealed class SpotManager : IZLinkSpotManager
    {
        private readonly Dictionary<RoutingId, ZLinkSpotInfo> _spots = [];

        public async ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            return await CreateAsync<TSpot>(ZLinkMessage.Empty, cancellationToken);
        }

        public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
            ZLinkMessage request,
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            _ = request;
            var rid = RoutingId.From(typeof(TSpot).Name);
            _spots[rid] = new ZLinkSpotInfo(rid);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(rid, ZLinkSpotCreateState.Created, null));
        }

        public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
            RoutingId spotRid,
            ZLinkMessage request,
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            _ = request;
            var created = !_spots.ContainsKey(spotRid);
            _spots[spotRid] = new ZLinkSpotInfo(spotRid);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(
                spotRid,
                created ? ZLinkSpotCreateState.Created : ZLinkSpotCreateState.Existing,
                null));
        }

        public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
            RoutingId spotRid,
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            return await GetOrCreateAsync<TSpot>(spotRid, ZLinkMessage.Empty, cancellationToken);
        }

        public ValueTask<ZLinkSpotInfo?> FindAsync(RoutingId spotRid,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<ZLinkSpotInfo?>(_spots.TryGetValue(spotRid, out var info) ? info : null);
        }

        public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<IReadOnlyList<ZLinkSpotInfo>>(_spots.Values.ToArray());
        }

        public ValueTask<bool> CloseAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult(_spots.Remove(spotRid));
        }
    }

    private sealed class SpotOutbound : IZLinkSpotOutbound, IZLinkSpotClient
    {
        public IZLinkSendCall SendToSpot<TMessage>(SpotHandle address, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToSpot<TMessage>(SpotHandle address, TMessage request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message)
        {
            return new PublishCall();
        }

        public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
        {
            return new SendCall();
        }

        public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
        {
            return new RequestCall(new JoinedRoom("room-1"));
        }
    }

    private sealed class SpotPublisherClient : IZLinkSpotPublisherClient
    {
        public IZLinkPublishCall PublishSpot<TEvent>(
            string channelName,
            string topic,
            TEvent message)
        {
            return new PublishCall();
        }
    }

    private sealed class SendCall : IZLinkSendCall
    {
        public IZLinkSendCall Metadata(string key, string value) => this;

        public IZLinkSendCall Metadata(ZLinkMessageMetadata metadata) => this;

        public ZLinkSubmitResult TrySubmit() => new(ZLinkSubmitStatus.Submitted);

        public ValueTask<ZLinkSubmitResult> SubmitAsync(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(TrySubmit());
    }

    private sealed class RequestCall(object reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall Metadata(string key, string value) => this;

        public IZLinkRequestCall Metadata(ZLinkMessageMetadata metadata) => this;

        public IZLinkRequestCall Timeout(TimeSpan timeout)
        {
            return this;
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult((TReply)reply);
        }

        public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
        {
            return Async<TReply>(cancellationToken);
        }

    }

    private sealed class PublishCall : IZLinkPublishCall
    {
        public IZLinkPublishCall Metadata(string key, string value) => this;

        public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata) => this;

        public ZLinkPublishResult TrySubmit() => new(ZLinkSubmitStatus.Submitted, default);

        public ValueTask<ZLinkPublishResult> SubmitAsync(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(TrySubmit());
    }

    private sealed class Timer : IZLinkTimer
    {
        public bool IsDisposed { get; private set; }

        public ValueTask CancelAsync()
        {
            IsDisposed = true;
            return ValueTask.CompletedTask;
        }

        public ValueTask DisposeAsync()
        {
            IsDisposed = true;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RoomPacketHandler : IZLinkSpotPacketHandler<RoomSpot, RoomEvent>
    {
        public ValueTask HandleAsync(RoomSpot spot, RoomEvent message, CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RoomRequestHandler : IZLinkSpotRequestHandler<RoomSpot, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(RoomSpot spot, JoinRoom request, CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new JoinedRoom(request.RoomId));
        }
    }

    private sealed class RoomEventHandler : IZLinkSpotSubscriptionHandler<RoomSpot, RoomEvent>
    {
        public ValueTask HandleAsync(RoomSpot spot, RoomEvent message, CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RoomTimerHandler : IZLinkSpotTimerHandler<RoomSpot>
    {
        public ValueTask HandleAsync(RoomSpot spot, ZLinkTimerTick tick, CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class PlayerActorPacketHandler : IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class PlayerActorSendHandler : IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class
        PlayerActorRequestHandler : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            JoinRoom request,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new JoinedRoom($"actor:{request.RoomId}"));
        }
    }

    private sealed class EntryActorSendHandler : IZLinkEntrySpotActorSendHandler<EntrySpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class
        EntryActorRequestHandler : IZLinkEntrySpotActorRequestHandler<EntrySpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            JoinRoom request,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new JoinedRoom($"entry:{request.RoomId}"));
        }
    }
}
