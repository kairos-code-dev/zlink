using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Spots;

public sealed class SpotContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkSpot),
        typeof(IZLinkEntrySpot),
        typeof(IZLinkActorHandlerRegistry),
        typeof(IZLinkSpotHandlerRegistry),
        typeof(IZLinkSpotOutbound),
        typeof(IZLinkSpotContext),
        typeof(IZLinkEntrySpotContext),
        typeof(IZLinkTimer))]
    public async Task Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages()
    {
        var context = new SpotContext(RoutingId.From("room-1"));
        IZLinkSpot spot = new RoomSpot(context);
        var entryContext = new EntrySpotContext(RoutingId.From("entry"));
        IZLinkEntrySpot entrySpot = new EntrySpot(entryContext);
        var actor = new PlayerActor("player-1");

        spot.Configure();
        entrySpot.Configure();
        context.Handlers.AddHandler<RoomPacketHandler>();
        context.Handlers.AddHandler<RoomPacketHandler>("room.packet");
        context.Handlers.AddPacket<RoomPacketHandler>();
        context.Handlers.AddSubscribe<RoomEventHandler>("room.events");
        context.Handlers.AddActorPacket<PlayerActorPacketHandler, PlayerActor>();
        context.Handlers.AddActorPacket<PlayerActorPacketHandler, PlayerActor>("actor.packet");
        context.Handlers.AddActorJoin<PlayerJoinHandler, PlayerActor, JoinRoom, JoinedRoom>();
        context.Handlers.AddActorJoin<AttributePlayerJoinHandler>();
        context.Handlers.AddPostActorJoined<PlayerJoinedHandler, PlayerActor>();
        context.Handlers.AddActorLeft<PlayerLeftHandler, PlayerActor>();
        context.Handlers.AddActorDisconnected<PlayerDisconnectedHandler, PlayerActor>();
        await context.LeaveActorAsync(actor);
        await context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
        await context.Outbound.SendSpot(RoutingId.From("room-2"), new RoomEvent("opened")).Submit();
        await context.Outbound.RequestSpot(RoutingId.From("room-2"), new JoinRoom("room-2")).SubmitAsync<JoinedRoom>();
        await context.Outbound.Publish("room.events", new RoomEvent("opened")).Submit();
        await context.Outbound.SendChannel("api", new RoomEvent("opened")).Submit();
        await context.Outbound.RequestChannel("api", new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();

        await spot.OnCreateAsync([new Message()], CancellationToken.None);
        await spot.OnInitializeAsync(CancellationToken.None);
        await spot.OnClosingAsync(CancellationToken.None);
        await entrySpot.OnInitializeAsync(CancellationToken.None);
        await entrySpot.OnClosingAsync(CancellationToken.None);

        Assert.Equal(["player-1"], context.LeftActors);
        Assert.Contains("heartbeat", context.Timers);
        Assert.Equal(RoutingId.From("entry"), entryContext.SpotRid);
    }

    [Fact]
    [ContractExample(typeof(IZLinkSpotOutbound))]
    public async Task Spot_outbound_context_exposes_all_routed_channel_and_publish_methods()
    {
        IZLinkSpotOutbound spotOutbound = new SpotContext(RoutingId.From("room-1"));
        IZLinkSpotOutbound entryOutbound = new EntrySpotContext(RoutingId.From("entry"));

        await spotOutbound.SendSpot(RoutingId.From("room-2"), new RoomEvent("spot-send")).Submit();
        await spotOutbound.RequestSpot(RoutingId.From("room-2"), new JoinRoom("room-2")).SubmitAsync<JoinedRoom>();
        await spotOutbound.Publish("room.events", new RoomEvent("spot-publish")).Submit();
        await spotOutbound.SendChannel("api", new RoomEvent("spot-channel-send")).Submit();
        await spotOutbound.RequestChannel("api", new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();

        await entryOutbound.SendSpot(RoutingId.From("room-2"), new RoomEvent("entry-send")).Submit();
        await entryOutbound.RequestSpot(RoutingId.From("room-2"), new JoinRoom("room-2")).SubmitAsync<JoinedRoom>();
        await entryOutbound.Publish("room.events", new RoomEvent("entry-publish")).Submit();
        await entryOutbound.SendChannel("api", new RoomEvent("entry-channel-send")).Submit();
        await entryOutbound.RequestChannel("api", new JoinRoom("entry")).SubmitAsync<JoinedRoom>();
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotManager),
        typeof(IZLinkSpotOutbound),
        typeof(IZLinkSpotPublisherClient),
        typeof(IZLinkSpotRemoteAddressResolver))]
    public async Task Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels()
    {
        var manager = new SpotManager();
        var created = await manager.GetOrCreateAsync<RoomSpot>(
            RoutingId.From("room-1"),
            [new Message()]);
        var routeResolver = new SpotRemoteAddressResolver(created.SpotRid);
        var route = await routeResolver.ResolveSpotRemoteAddressAsync(created.SpotRid, CancellationToken.None);

        var localClient = new SpotOutbound();
        await localClient.SendSpot(created.SpotRid, new RoomEvent("opened")).Submit();
        var reply = await localClient.RequestSpot(created.SpotRid, new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();

        IZLinkSpotPublisherClient publisher = new SpotPublisherClient();
        await publisher.PublishSpot("play-events", "room.events", new RoomEvent("opened")).Submit();

        Assert.True(created.Created);
        Assert.Equal("play-router", route.RouterChannelId);
        Assert.Equal("room-1", reply.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotPacketHandler<,>),
        typeof(IZLinkSpotRequestHandler<,,>),
        typeof(IZLinkSpotSubscriptionHandler<,>),
        typeof(IZLinkSpotTimerHandler<>),
        typeof(IZLinkSpotActorJoinHandler<,,,>),
        typeof(IZLinkSpotActorSendHandler<,,>),
        typeof(IZLinkSpotActorRequestHandler<,,,>),
        typeof(IZLinkSpotPostActorJoinedHandler<,>),
        typeof(IZLinkSpotActorLeftHandler<,>),
        typeof(IZLinkSpotActorDisconnectedHandler<,>),
        typeof(IZLinkEntrySpotActorSendHandler<,,>),
        typeof(IZLinkEntrySpotActorRequestHandler<,,,>),
        typeof(IZLinkEntrySpotActorDisconnectedHandler<,>))]
    public async Task Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it()
    {
        var room = new RoomSpot(new SpotContext(RoutingId.From("room-1")));
        var entry = new EntrySpot(new EntrySpotContext(RoutingId.From("entry")));
        var actor = new PlayerActor("player-1");

        await new RoomPacketHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        var roomReply = await new RoomRequestHandler().HandleAsync(room, new JoinRoom("room-1"), CancellationToken.None);
        await new RoomEventHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        await new RoomTimerHandler().HandleAsync(room, TimerTick(), CancellationToken.None);
        var joinReply = await new PlayerJoinHandler().HandleAsync(room, actor, new JoinRoom("room-1"), CancellationToken.None);
        await new PlayerActorSendHandler().HandleAsync(room, actor, null!, new RoomEvent("opened"), CancellationToken.None);
        var actorReply = await new PlayerActorRequestHandler().HandleAsync(room, actor, null!, new JoinRoom("room-1"), CancellationToken.None);
        await new PlayerJoinedHandler().HandleAsync(room, actor, Lifecycle(ZLinkSpotActorChangeKind.JoinSpot), CancellationToken.None);
        await new PlayerLeftHandler().HandleAsync(room, actor, Lifecycle(ZLinkSpotActorChangeKind.LeaveSpot), CancellationToken.None);
        await new PlayerDisconnectedHandler().HandleAsync(room, actor, CancellationToken.None);
        await new EntryActorSendHandler().HandleAsync(entry, actor, null!, new RoomEvent("opened"), CancellationToken.None);
        var entryReply = await new EntryActorRequestHandler().HandleAsync(entry, actor, null!, new JoinRoom("room-1"), CancellationToken.None);
        await new EntryActorJoinedHandler().HandleAsync(entry, actor, Lifecycle(ZLinkSpotActorChangeKind.JoinEntrySpot), CancellationToken.None);
        await new EntryActorLeftHandler().HandleAsync(entry, actor, Lifecycle(ZLinkSpotActorChangeKind.LeaveSpot), CancellationToken.None);
        await new EntryActorDisconnectedHandler().HandleAsync(entry, actor, CancellationToken.None);

        Assert.Equal("room-1", roomReply.RoomId);
        Assert.Equal("room-1", joinReply.RoomId);
        Assert.Equal("actor:room-1", actorReply.RoomId);
        Assert.Equal("entry:room-1", entryReply.RoomId);
    }

    [Fact]
    public void Spot_actor_change_kind_contains_only_membership_changes()
    {
        var values = Enum.GetValues<ZLinkSpotActorChangeKind>();

        Assert.Equal(
            [
                ZLinkSpotActorChangeKind.JoinSpot,
                ZLinkSpotActorChangeKind.JoinEntrySpot,
                ZLinkSpotActorChangeKind.LeaveSpot
            ],
            values);
        Assert.DoesNotContain(values, static value => value.ToString() == "Unknown");
        Assert.DoesNotContain(values, static value => value.ToString() == "Disconnect");
        Assert.DoesNotContain(values, static value => value.ToString() == "Destroy");
        Assert.Throws<ArgumentOutOfRangeException>(
            static () => new ZLinkSpotActorChangeResult((ZLinkSpotActorChangeKind)0));
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
    public void Spot_actor_contexts_expose_only_dispatch_metadata()
    {
        AssertContextProperties<ZLinkSpotActorSendContext>(
            nameof(IZLinkHandlerContext.ChannelName),
            nameof(IZLinkHandlerContext.ConnectionAborted),
            nameof(IZLinkHandlerContext.ContentType),
            nameof(ZLinkSpotActorSendContext.Metadata),
            nameof(IZLinkHandlerContext.PacketName));

        AssertContextProperties<ZLinkSpotActorRequestContext>(
            nameof(IZLinkHandlerContext.ChannelName),
            nameof(IZLinkHandlerContext.ConnectionAborted),
            nameof(IZLinkHandlerContext.ContentType),
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

    private static ZLinkSpotActorChangeResult Lifecycle(ZLinkSpotActorChangeKind kind) =>
        new(kind);

    private static ZLinkTimerTick TimerTick() =>
        new(
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

    private sealed record JoinRoom(string RoomId);

    private sealed record JoinedRoom(string RoomId);

    private sealed record RoomEvent(string State);

    private sealed class RoomSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    private sealed class EntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;
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
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.From("spot-node");

        public IZLinkSpotHandlerRegistry Handlers => this;

        public IZLinkSpotOutbound Outbound => this;

        public List<string> JoinedActors { get; } = [];

        public List<string> LeftActors { get; } = [];

        public List<string> Timers { get; } = [];

        public void AddHandler<THandler>()
            where THandler : class { }

        public void AddHandler<THandler>(string packetName)
            where THandler : class { }

        public void AddActorPacket<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorPacket<THandler, TActor>(string packetName)
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddPostActorJoined<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorLeft<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorDisconnected<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class { }

        public void AddActorJoin<THandler, TActor, TRequest, TReply>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorJoin<THandler>()
            where THandler : class { }

        public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestSpot<TRequest>(RoutingId spotRid, TRequest request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message) => new PublishCall();

        public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public ValueTask LeaveActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default)
        {
            LeftActors.Add(actor.ActorId);
            return ValueTask.CompletedTask;
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
    }

    private sealed class EntrySpotContext(RoutingId spotRid) :
        IZLinkEntrySpotContext,
        IZLinkSpotHandlerRegistry,
        IZLinkSpotOutbound
    {
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.From("spot-node");

        public IZLinkSpotHandlerRegistry Handlers => this;

        public IZLinkSpotOutbound Outbound => this;

        public void AddHandler<THandler>()
            where THandler : class { }

        public void AddHandler<THandler>(string packetName)
            where THandler : class { }

        public void AddActorPacket<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorPacket<THandler, TActor>(string packetName)
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddPostActorJoined<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorLeft<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorDisconnected<THandler, TActor>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class { }

        public void AddActorJoin<THandler, TActor, TRequest, TReply>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorJoin<THandler>()
            where THandler : class { }

        public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestSpot<TRequest>(RoutingId spotRid, TRequest request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message) => new PublishCall();

        public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public ValueTask<IZLinkTimer> AddTimer<THandler>(
            string name,
            TimeSpan period,
            ZLinkTimerOptions? options = null,
            CancellationToken cancellationToken = default)
            where THandler : class =>
            ValueTask.FromResult<IZLinkTimer>(new Timer());
    }

    private sealed class SpotManager : IZLinkSpotManager
    {
        private readonly Dictionary<RoutingId, ZLinkSpotInfo> _spots = [];

        public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot =>
            CreateAsync<TSpot>([], cancellationToken);

        public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            var rid = RoutingId.From(typeof(TSpot).Name);
            _spots[rid] = new ZLinkSpotInfo(rid);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(rid, true));
        }

        public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
            RoutingId spotRid,
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken = default)
            where TSpot : IZLinkSpot
        {
            var created = !_spots.ContainsKey(spotRid);
            _spots[spotRid] = new ZLinkSpotInfo(spotRid);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(spotRid, created));
        }

        public ValueTask<ZLinkSpotInfo?> FindAsync(RoutingId spotRid,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_spots.TryGetValue(spotRid, out var info) ? info : (ZLinkSpotInfo?)null);

        public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IReadOnlyList<ZLinkSpotInfo>>(_spots.Values.ToArray());

        public ValueTask<bool> RemoveAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(_spots.Remove(spotRid));
    }

    private sealed class SpotRemoteAddressResolver(RoutingId configuredSpotRid) : IZLinkSpotRemoteAddressResolver
    {
        public ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ZLinkSpotRemoteAddress(
                "play-router",
                RoutingId.From("spot-node"),
                spotRid,
                configuredSpotRid == spotRid ? ZLinkSpotKind.User : ZLinkSpotKind.Invalid));
    }

    private sealed class SpotOutbound : IZLinkSpotOutbound
    {
        public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestSpot<TMessage>(RoutingId spotRid, TMessage request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message) => new PublishCall();

        public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestChannel<TMessage>(string channelName, TMessage request) =>
            new RequestCall(new JoinedRoom("room-1"));
    }

    private sealed class SpotPublisherClient : IZLinkSpotPublisherClient
    {
        public IZLinkPublishCall PublishSpot<TEvent>(
            string channelName,
            string topic,
            TEvent message) =>
            new PublishCall();
    }

    private sealed class SendCall : IZLinkSendCall
    {
        public IZLinkSendCall PacketName(string messageName) => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class RequestCall(object reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall PacketName(string messageName) => this;

        public IZLinkRequestCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply);
    }

    private sealed class PublishCall : IZLinkPublishCall
    {
        public IZLinkPublishCall PacketName(string messageName) => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class Timer : IZLinkTimer
    {
        public bool IsDisposed { get; private set; }

        public ValueTask CancelAsync(CancellationToken cancellationToken = default)
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
        public ValueTask HandleAsync(RoomSpot spot, RoomEvent message, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class RoomRequestHandler : IZLinkSpotRequestHandler<RoomSpot, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(RoomSpot spot, JoinRoom request, CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom(request.RoomId));
    }

    private sealed class RoomEventHandler : IZLinkSpotSubscriptionHandler<RoomSpot, RoomEvent>
    {
        public ValueTask HandleAsync(RoomSpot spot, RoomEvent message, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class RoomTimerHandler : IZLinkSpotTimerHandler<RoomSpot>
    {
        public ValueTask HandleAsync(RoomSpot spot, ZLinkTimerTick tick, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerJoinHandler : IZLinkSpotActorJoinHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            JoinRoom request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom(request.RoomId));
    }

    private sealed class AttributePlayerJoinHandler;

    private sealed class PlayerActorPacketHandler : IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerActorSendHandler : IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerActorRequestHandler : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            JoinRoom request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom($"actor:{request.RoomId}"));
    }

    private sealed class PlayerJoinedHandler : IZLinkSpotPostActorJoinedHandler<RoomSpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorChangeResult context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerLeftHandler : IZLinkSpotActorLeftHandler<RoomSpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorChangeResult context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerDisconnectedHandler : IZLinkSpotActorDisconnectedHandler<RoomSpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorSendHandler : IZLinkEntrySpotActorSendHandler<EntrySpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorSendContext context,
            RoomEvent message,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorRequestHandler : IZLinkEntrySpotActorRequestHandler<EntrySpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorRequestContext context,
            JoinRoom request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom($"entry:{request.RoomId}"));
    }

    private sealed class EntryActorJoinedHandler : IZLinkSpotPostActorJoinedHandler<EntrySpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorChangeResult context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorLeftHandler : IZLinkSpotActorLeftHandler<EntrySpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorChangeResult context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorDisconnectedHandler : IZLinkEntrySpotActorDisconnectedHandler<EntrySpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }
}
