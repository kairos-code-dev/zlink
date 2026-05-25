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
        typeof(IZLinkSpotOutboundContext),
        typeof(IZLinkSpotContext),
        typeof(IZLinkEntrySpotContext),
        typeof(IZLinkTimer))]
    public async Task Spot_context_registers_handlers_timers_actor_lifecycle_and_outbound_messages()
    {
        var context = new SpotContext("room", RoutingId.Of("room-1"));
        IZLinkSpot spot = new RoomSpot(context);
        var entryContext = new EntrySpotContext(RoutingId.Of("entry"));
        IZLinkEntrySpot entrySpot = new EntrySpot(entryContext);
        var actor = new PlayerActor("player-1");

        spot.Configure();
        entrySpot.Configure();
        context.AddHandler<RoomPacketHandler>();
        context.AddHandler<RoomPacketHandler>("room.packet");
        context.AddPacket<RoomPacketHandler>();
        context.AddSubscribe<RoomEventHandler>("room.events");
        context.AddActorPacket<PlayerActorPacketHandler, PlayerActor>();
        context.AddActorPacket<PlayerActorPacketHandler, PlayerActor>("actor.packet");
        context.AddActorJoin<PlayerJoinHandler, PlayerActor, JoinRoom, JoinedRoom>();
        context.AddActorJoin<AttributePlayerJoinHandler>();
        context.AddPostActorJoined<PlayerJoinedHandler, PlayerActor>();
        context.AddActorLeft<PlayerLeftHandler, PlayerActor>();
        await context.JoinActorAsync(actor);
        await context.LeaveActorAsync(actor);
        await context.AddTimer<RoomTimerHandler>("heartbeat", TimeSpan.FromSeconds(1));
        await context.Publish("room.events", new RoomEvent("opened")).Submit();
        await context.SendChannel("api", new RoomEvent("opened")).Submit();
        await context.RequestChannel("api", new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();

        await spot.OnCreateAsync([new Message()], CancellationToken.None);
        await spot.OnInitializeAsync(CancellationToken.None);
        await spot.OnClosingAsync(CancellationToken.None);
        await entrySpot.OnInitializeAsync(CancellationToken.None);
        await entrySpot.OnClosingAsync(CancellationToken.None);

        Assert.Equal("room", context.SpotName);
        Assert.Equal(["player-1"], context.JoinedActors);
        Assert.Equal(["player-1"], context.LeftActors);
        Assert.Contains("heartbeat", context.Timers);
        Assert.Equal(RoutingId.Of("entry"), entryContext.SpotRid);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkSpotManager),
        typeof(IZLinkSpotClient),
        typeof(IZLinkRoutedSpotClient),
        typeof(IZLinkRoutedSpotChannelClient),
        typeof(IZLinkSpotRef),
        typeof(IZLinkSpotMeshPublisherClient),
        typeof(IZLinkSpotPublisherClient),
        typeof(IZLinkSpotConnectionManager),
        typeof(IZLinkSpotRemoteAddressResolver))]
    public async Task Spot_clients_separate_local_spot_api_routed_egress_and_publisher_channels()
    {
        var manager = new SpotManager();
        var created = await manager.GetOrCreateAsync(
            "room",
            RoutingId.Of("room-1"),
            [new Message()]);
        var routeResolver = new SpotRemoteAddressResolver(created.SpotRid);
        var route = await routeResolver.ResolveSpotRemoteAddressAsync(created.SpotRid, CancellationToken.None);

        var localClient = new SpotClient();
        await localClient.SendSpot(created.SpotRid, new RoomEvent("opened")).Submit();
        var reply = await localClient.RequestSpot(created.SpotName, new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();
        await localClient.SendChannel("api", new RoomEvent("opened")).Submit();
        await localClient.RequestChannel("api", new JoinRoom("room-1")).SubmitAsync<JoinedRoom>();
        await localClient.Publish("room.events", new RoomEvent("opened")).Submit();

        var routedClient = new RoutedSpotClient();
        var spotRef = await routedClient.BindSpotHandleAsync(route);
        await routedClient
            .ViaEgressChannel("play-router")
            .RequestSpot(spotRef, new JoinRoom("room-1"))
            .SubmitAsync<JoinedRoom>();

        IZLinkSpotPublisherClient publisher = new SpotPublisherClient();
        await publisher.Publish("play-events", "room.events", new RoomEvent("opened")).Submit();

        var connections = await new SpotConnectionManager().GetRouterAsync("play-spots");
        await connections.ConnectAsync("tcp://127.0.0.1:5500");

        Assert.True(created.Created);
        Assert.Equal("play-router", route.RouterChannelId);
        Assert.Equal("room-1", reply.RoomId);
        Assert.Equal(["tcp://127.0.0.1:5500"], await connections.ListConnectionsAsync());
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
        typeof(IZLinkEntrySpotActorSendHandler<,,>),
        typeof(IZLinkEntrySpotActorRequestHandler<,,,>))]
    public async Task Spot_handlers_receive_the_spot_instance_and_actor_when_the_contract_requires_it()
    {
        var room = new RoomSpot(new SpotContext("room", RoutingId.Of("room-1")));
        var entry = new EntrySpot(new EntrySpotContext(RoutingId.Of("entry")));
        var actor = new PlayerActor("player-1");

        await new RoomPacketHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        var roomReply = await new RoomRequestHandler().HandleAsync(room, new JoinRoom("room-1"), CancellationToken.None);
        await new RoomEventHandler().HandleAsync(room, new RoomEvent("opened"), CancellationToken.None);
        await new RoomTimerHandler().HandleAsync(room, TimerTick(), CancellationToken.None);
        var joinReply = await new PlayerJoinHandler().HandleAsync(room, actor, new JoinRoom("room-1"), CancellationToken.None);
        await new PlayerActorSendHandler().HandleAsync(room, actor, new RoomEvent("opened"), CancellationToken.None);
        var actorReply = await new PlayerActorRequestHandler().HandleAsync(room, actor, new JoinRoom("room-1"), CancellationToken.None);
        await new PlayerJoinedHandler().HandleAsync(room, actor, Lifecycle(ZLinkSpotActorLifecycleReason.JoinSpot), CancellationToken.None);
        await new PlayerLeftHandler().HandleAsync(room, actor, Lifecycle(ZLinkSpotActorLifecycleReason.LeaveSpot), CancellationToken.None);
        await new EntryActorSendHandler().HandleAsync(entry, actor, new RoomEvent("opened"), CancellationToken.None);
        var entryReply = await new EntryActorRequestHandler().HandleAsync(entry, actor, new JoinRoom("room-1"), CancellationToken.None);
        await new EntryActorJoinedHandler().HandleAsync(entry, actor, Lifecycle(ZLinkSpotActorLifecycleReason.JoinEntrySpot), CancellationToken.None);
        await new EntryActorLeftHandler().HandleAsync(entry, actor, Lifecycle(ZLinkSpotActorLifecycleReason.LeaveSpot), CancellationToken.None);

        Assert.Equal("room-1", roomReply.RoomId);
        Assert.Equal("room-1", joinReply.RoomId);
        Assert.Equal("actor:room-1", actorReply.RoomId);
        Assert.Equal("entry:room-1", entryReply.RoomId);
    }

    private static ZLinkSpotActorLifecycleContext Lifecycle(ZLinkSpotActorLifecycleReason reason) =>
        new(
            RoutingId.Of("room-1"),
            RoutingId.Of("room-2"),
            1,
            reason,
            0);

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

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class SpotContext(string spotName, RoutingId spotRid) : IZLinkSpotContext
    {
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.Of("spot-node");

        public string SpotName { get; } = spotName;

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

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class { }

        public void AddActorJoin<THandler, TActor, TRequest, TReply>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorJoin<THandler>()
            where THandler : class { }

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message) => new PublishCall();

        public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestChannel<TRequest>(string channelName, TRequest request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public ValueTask JoinActorAsync(
            IZLinkActor actor,
            CancellationToken cancellationToken = default)
        {
            JoinedActors.Add(actor.ActorId);
            return ValueTask.CompletedTask;
        }

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

    private sealed class EntrySpotContext(RoutingId spotRid) : IZLinkEntrySpotContext
    {
        public RoutingId SpotRid { get; } = spotRid;

        public RoutingId NodeRid => RoutingId.Of("spot-node");

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

        public void AddPacket<THandler>()
            where THandler : class { }

        public void AddSubscribe<THandler>(string topic)
            where THandler : class { }

        public void AddActorJoin<THandler, TActor, TRequest, TReply>()
            where THandler : class
            where TActor : IZLinkActor { }

        public void AddActorJoin<THandler>()
            where THandler : class { }

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

        public ValueTask<ZLinkSpotCreateResult> CreateAsync(
            string spotName,
            CancellationToken cancellationToken = default) =>
            CreateAsync(spotName, [], cancellationToken);

        public ValueTask<ZLinkSpotCreateResult> CreateAsync(
            string spotName,
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken = default)
        {
            var rid = RoutingId.Of(spotName);
            _spots[rid] = new ZLinkSpotInfo(rid, spotName);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(rid, spotName, true));
        }

        public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
            string spotName,
            RoutingId spotRid,
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken = default)
        {
            var created = !_spots.ContainsKey(spotRid);
            _spots[spotRid] = new ZLinkSpotInfo(spotRid, spotName);
            return ValueTask.FromResult(new ZLinkSpotCreateResult(spotRid, spotName, created));
        }

        public ValueTask<ZLinkSpotInfo?> GetAsync(
            RoutingId spotRid,
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
            string spotName,
            CancellationToken cancellationToken) =>
            ResolveSpotRemoteAddressAsync(RoutingId.Of(spotName), cancellationToken);

        public ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ZLinkSpotRemoteAddress(
                "play-router",
                RoutingId.Of("spot-node"),
                spotRid,
                configuredSpotRid == spotRid ? ZLinkSpotKind.User : ZLinkSpotKind.Invalid));
    }

    private sealed class SpotClient : IZLinkSpotClient
    {
        public IZLinkSendCall SendSpot<TMessage>(string spotName, TMessage message) => new SendCall();

        public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestSpot<TMessage>(string spotName, TMessage request) =>
            new RequestCall(new JoinedRoom(spotName == "room" ? "room-1" : spotName));

        public IZLinkRequestCall RequestSpot<TMessage>(RoutingId spotRid, TMessage request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public IZLinkSendCall SendChannel<TMessage>(string channelName, TMessage message) => new SendCall();

        public IZLinkRequestCall RequestChannel<TMessage>(string channelName, TMessage request) =>
            new RequestCall(new JoinedRoom("room-1"));

        public IZLinkPublishCall Publish<TEvent>(string topic, TEvent message) => new PublishCall();
    }

    private sealed class RoutedSpotClient : IZLinkRoutedSpotClient
    {
        public IZLinkRoutedSpotChannelClient ViaEgressChannel(string localEgressChannelName) =>
            new RoutedSpotChannelClient(localEgressChannelName);

        public ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
            ZLinkSpotRemoteAddress remoteAddress,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkSpotRef>(new SpotRef(remoteAddress));

        public ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
            IZLinkSpotRef spot,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(spot);
    }

    private sealed class RoutedSpotChannelClient(string egressChannel) : IZLinkRoutedSpotChannelClient
    {
        public IZLinkSendCall SendSpot<TMessage>(RoutingId spotRid, TMessage message) => new SendCall();

        public IZLinkSendCall SendSpot<TMessage>(IZLinkSpotRef spot, TMessage message) =>
            SendSpot(spot.SpotRid, message);

        public IZLinkRequestCall RequestSpot<TRequest>(RoutingId spotRid, TRequest request) =>
            new RequestCall(new JoinedRoom(egressChannel == "play-router" ? "room-1" : "unknown"));

        public IZLinkRequestCall RequestSpot<TRequest>(IZLinkSpotRef spot, TRequest request) =>
            RequestSpot(spot.SpotRid, request);
    }

    private sealed class SpotRef(ZLinkSpotRemoteAddress remoteAddress) : IZLinkSpotRef
    {
        public RoutingId SpotRid => remoteAddress.SpotRid;

        public string? SpotName => null;

        public ZLinkSpotKind SpotKind => remoteAddress.SpotKind;

        public bool IsRemote => true;

        public ZLinkSpotRemoteAddress RemoteAddress => remoteAddress;
    }

    private sealed class SpotPublisherClient : IZLinkSpotPublisherClient
    {
        public IZLinkPublishCall Publish<TEvent>(
            string channelName,
            string topic,
            TEvent message) =>
            new PublishCall();
    }

    private sealed class SpotConnectionManager : IZLinkSpotConnectionManager
    {
        private readonly EndpointConnections _connections = new();

        public ValueTask<IZLinkEndpointConnections> GetRouterAsync(
            string spotNodeName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
            string spotNodeName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetClientServerChannelClientAsync(
            string spotNodeName,
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetSpotMeshPublisherClientAsync(
            string spotNodeName,
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetChannelClientAsync(
            string spotNodeName,
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetSpotPublisherClientAsync(
            string spotNodeName,
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);
    }

    private sealed class EndpointConnections : IZLinkEndpointConnections
    {
        private readonly List<string> _endpoints = [];

        public ValueTask<bool> ConnectAsync(string endpoint, CancellationToken cancellationToken = default)
        {
            _endpoints.Add(endpoint);
            return ValueTask.FromResult(true);
        }

        public ValueTask DisconnectAsync(string endpoint, CancellationToken cancellationToken = default)
        {
            _endpoints.Remove(endpoint);
            return ValueTask.CompletedTask;
        }

        public ValueTask<IReadOnlyList<string>> ListConnectionsAsync(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IReadOnlyList<string>>(_endpoints.ToArray());
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
        public ValueTask HandleAsync(RoomSpot spot, PlayerActor actor, RoomEvent message, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerActorSendHandler : IZLinkSpotActorSendHandler<RoomSpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(RoomSpot spot, PlayerActor actor, RoomEvent message, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerActorRequestHandler : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            JoinRoom request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom($"actor:{request.RoomId}"));
    }

    private sealed class PlayerJoinedHandler : IZLinkSpotPostActorJoinedHandler<RoomSpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class PlayerLeftHandler : IZLinkSpotActorLeftHandler<RoomSpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            RoomSpot spot,
            PlayerActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorSendHandler : IZLinkEntrySpotActorSendHandler<EntrySpot, PlayerActor, RoomEvent>
    {
        public ValueTask HandleAsync(EntrySpot entrySpot, PlayerActor actor, RoomEvent message, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorRequestHandler : IZLinkEntrySpotActorRequestHandler<EntrySpot, PlayerActor, JoinRoom, JoinedRoom>
    {
        public ValueTask<JoinedRoom> HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            JoinRoom request,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new JoinedRoom($"entry:{request.RoomId}"));
    }

    private sealed class EntryActorJoinedHandler : IZLinkSpotPostActorJoinedHandler<EntrySpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class EntryActorLeftHandler : IZLinkSpotActorLeftHandler<EntrySpot, PlayerActor>
    {
        public ValueTask HandleAsync(
            EntrySpot entrySpot,
            PlayerActor actor,
            ZLinkSpotActorLifecycleContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }
}
