using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Channels;

public sealed class ChannelContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkClientServerClient),
        typeof(IZLinkClient),
        typeof(IZLinkSendCall),
        typeof(IZLinkRequestCall))]
    public async Task Client_server_client_sends_and_requests_by_channel_name()
    {
        var client = new ExampleClient();

        await client
            .Send("api", new AuthenticateRequest("player-1"))
            .PacketName("authenticate")
            .Submit();

        var reply = await client
            .Request("api", new AuthenticateRequest("player-1"))
            .PacketName("authenticate")
            .Timeout(TimeSpan.FromSeconds(3))
            .SubmitAsync<AuthenticateReply>();

        Assert.Equal("api", client.LastChannelName);
        Assert.Equal("authenticate", client.LastPacketName);
        Assert.Equal("player-1", reply.PlayerId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkRouteClient),
        typeof(IZLinkRouteSendCall),
        typeof(IZLinkRouteRequestCall),
        typeof(IZLinkRouteSendHandler<>),
        typeof(IZLinkRouteRequestHandler<,>))]
    public async Task Route_client_addresses_a_target_node_through_a_router_channel()
    {
        var client = new ExampleRouteClient();
        var target = RoutingId.Of("play-node-1");

        await client
            .SendTo("play-router", target, new RoomEvent("opened"))
            .PacketName("room.event")
            .Submit();

        var room = await client
            .RequestTo("play-router", target, new AllocateRoom("alice"))
            .PacketName("room.allocate")
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync<RoomAllocated>();

        Assert.Equal("play-router", client.RouterChannelId);
        Assert.Equal(target, client.TargetNodeRid);
        Assert.Equal("room-1", room.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkFanoutPublisher),
        typeof(IZLinkEventPublisher),
        typeof(IZLinkPublishCall),
        typeof(IZLinkChannelConnectionManager))]
    public async Task Fanout_publisher_publishes_events_to_a_topic()
    {
        var publisher = new ExampleFanoutPublisher();

        await publisher
            .Publish("events", "room.opened", new RoomEvent("opened"))
            .PacketName("room.event")
            .Submit();

        var manager = new ExampleChannelConnectionManager();
        var connections = await manager.GetFanoutSubscriberAsync("events");
        await connections.ConnectAsync("tcp://127.0.0.1:5001");

        Assert.Equal(("events", "room.opened", "room.event"), publisher.LastPublish);
        Assert.Equal(["tcp://127.0.0.1:5001"], await connections.ListConnectionsAsync());
    }

    private sealed record AuthenticateRequest(string PlayerId);

    private sealed record AuthenticateReply(string PlayerId);

    private sealed record AllocateRoom(string PlayerId);

    private sealed record RoomAllocated(string RoomId);

    private sealed record RoomEvent(string State);

    private sealed class ExampleClient : IZLinkClient
    {
        public string? LastChannelName { get; private set; }

        public string? LastPacketName { get; private set; }

        public IZLinkSendCall Send<TMessage>(string channelName, TMessage message)
        {
            LastChannelName = channelName;
            return new ExampleSendCall(packetName => LastPacketName = packetName);
        }

        public IZLinkRequestCall Request<TMessage>(string channelName, TMessage request)
        {
            LastChannelName = channelName;
            return new ExampleRequestCall(
                packetName => LastPacketName = packetName,
                typeof(TMessage) == typeof(AuthenticateRequest)
                    ? new AuthenticateReply(((AuthenticateRequest)(object)request!).PlayerId)
                    : null);
        }
    }

    private sealed class ExampleRouteClient : IZLinkRouteClient
    {
        public string? RouterChannelId { get; private set; }

        public RoutingId TargetNodeRid { get; private set; }

        public IZLinkRouteSendCall SendTo<TMessage>(
            string routerChannelId,
            RoutingId targetNodeRid,
            TMessage message)
        {
            RouterChannelId = routerChannelId;
            TargetNodeRid = targetNodeRid;
            return new ExampleRouteSendCall();
        }

        public IZLinkRouteRequestCall RequestTo<TRequest>(
            string routerChannelId,
            RoutingId targetNodeRid,
            TRequest request)
        {
            RouterChannelId = routerChannelId;
            TargetNodeRid = targetNodeRid;
            return new ExampleRouteRequestCall(new RoomAllocated("room-1"));
        }
    }

    private sealed class ExampleFanoutPublisher : IZLinkEventPublisher
    {
        public (string ChannelName, string Topic, string? PacketName) LastPublish { get; private set; }

        public IZLinkPublishCall Publish<TEvent>(
            string channelName,
            string topic,
            TEvent message)
        {
            LastPublish = (channelName, topic, null);
            return new ExamplePublishCall(packetName =>
                LastPublish = (LastPublish.ChannelName, LastPublish.Topic, packetName));
        }
    }

    private sealed class ExampleChannelConnectionManager : IZLinkChannelConnectionManager
    {
        private readonly ExampleEndpointConnections _connections = new();

        public ValueTask<IZLinkEndpointConnections> GetClientServerClientAsync(
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetFanoutSubscriberAsync(
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetClientAsync(
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);

        public ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
            string channelName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IZLinkEndpointConnections>(_connections);
    }

    private sealed class ExampleSendCall(Action<string> setPacketName) : IZLinkSendCall
    {
        public IZLinkSendCall PacketName(string messageName)
        {
            setPacketName(messageName);
            return this;
        }

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class ExampleRequestCall(Action<string> setPacketName, object? reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall PacketName(string messageName)
        {
            setPacketName(messageName);
            return this;
        }

        public IZLinkRequestCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply!);
    }

    private sealed class ExamplePublishCall(Action<string> setPacketName) : IZLinkPublishCall
    {
        public IZLinkPublishCall PacketName(string messageName)
        {
            setPacketName(messageName);
            return this;
        }

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class ExampleRouteSendCall : IZLinkRouteSendCall
    {
        public IZLinkRouteSendCall PacketName(string packetName) => this;

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private sealed class ExampleRouteRequestCall(object reply) : IZLinkRouteRequestCall
    {
        public IZLinkRouteRequestCall PacketName(string packetName) => this;

        public IZLinkRouteRequestCall Timeout(TimeSpan timeout) => this;

        public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default) =>
            ValueTask.FromResult((TReply)reply);
    }

    private sealed class ExampleEndpointConnections : IZLinkEndpointConnections
    {
        private readonly List<string> _endpoints = [];

        public ValueTask<bool> ConnectAsync(
            string endpoint,
            CancellationToken cancellationToken = default)
        {
            _endpoints.Add(endpoint);
            return ValueTask.FromResult(true);
        }

        public ValueTask DisconnectAsync(
            string endpoint,
            CancellationToken cancellationToken = default)
        {
            _endpoints.Remove(endpoint);
            return ValueTask.CompletedTask;
        }

        public ValueTask<IReadOnlyList<string>> ListConnectionsAsync(
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IReadOnlyList<string>>(_endpoints.ToArray());
    }

    private sealed class RoomEventRouteSendHandler : IZLinkRouteSendHandler<RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomEvent message,
            ZLinkRouteSendContext context,
            CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    private sealed class AllocateRoomRouteHandler : IZLinkRouteRequestHandler<AllocateRoom, RoomAllocated>
    {
        public ValueTask<RoomAllocated> HandleAsync(
            AllocateRoom request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new RoomAllocated("room-1"));
    }
}
