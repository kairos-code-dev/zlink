using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Channels;

public sealed class ChannelContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkChannelClient),
        typeof(IZLinkSendCall),
        typeof(IZLinkRequestCall))]
    public async Task Channel_client_sends_and_requests_by_channel_name()
    {
        var client = new ExampleClient();

        await client
            .SendToChannel("api", new AuthenticateRequest("player-1"))
            .PacketName("authenticate")
            .Submit();

        var reply = await client
            .RequestToChannel("api", new AuthenticateRequest("player-1"))
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
        typeof(IZLinkSendCall),
        typeof(IZLinkRequestCall),
        typeof(IZLinkRouteSendHandler<>),
        typeof(IZLinkRouteRequestHandler<,>))]
    public async Task Route_client_addresses_a_target_node_through_a_router_channel()
    {
        var client = new ExampleRouteClient();
        var target = RoutingId.From("play-node-1");

        await client
            .Send("play-router", target, new RoomEvent("opened"))
            .PacketName("room.event")
            .Submit();

        var room = await client
            .Request("play-router", target, new AllocateRoom("alice"))
            .PacketName("room.allocate")
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync<RoomAllocated>();

        Assert.Equal("play-router", client.RouterChannelId);
        Assert.Equal(target, client.TargetNodeRid);
        Assert.Equal("room-1", room.RoomId);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkFanoutClient),
        typeof(IZLinkPublishCall))]
    public async Task Fanout_client_publishes_events_to_a_topic()
    {
        var publisher = new ExampleFanoutPublisher();

        await publisher
            .Publish("events", "room.opened", new RoomEvent("opened"))
            .PacketName("room.event")
            .Submit();

        Assert.Equal(("events", "room.opened", "room.event"), publisher.LastPublish);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkChannelClient),
        typeof(IZLinkSendCall),
        typeof(IZLinkRequestCall),
        typeof(IZLinkFanoutClient),
        typeof(IZLinkPublishCall))]
    public async Task Channel_messaging_replaces_grpc_unary_command_and_streaming_for_web_services()
    {
        // The same channel-messaging surface a gRPC web backend would reach for:
        // unary RPC, fire-and-forget command, and a server-streamed status feed.
        var orders = new ExampleClient();
        var events = new ExampleFanoutPublisher();

        // gRPC unary RPC -> request/response on a logical channel name.
        var placed = await orders
            .RequestToChannel("orders", new PlaceOrder("order-1042", "acct-77", 18742))
            .PacketName("orders.place")
            .Timeout(TimeSpan.FromSeconds(2))
            .SubmitAsync<OrderPlaced>();

        // gRPC unary returning google.protobuf.Empty -> one-way send (no reply awaited).
        await orders
            .SendToChannel("inventory", new ReserveStock("order-1042", "sku-9", 3))
            .PacketName("inventory.reserve")
            .Submit();

        // gRPC server-streaming / event feed -> pub/sub fan-out to many subscribers.
        await events
            .Publish("order.events", "order.status", new OrderStatusChanged("order-1042", "Placed"))
            .PacketName("order.status-changed")
            .Submit();

        Assert.Equal("order-1042", placed.OrderId);          // unary RPC reply correlated by type
        Assert.Equal("inventory", orders.LastChannelName);    // last one-way send routed by channel name
        Assert.Equal(
            ("order.events", "order.status", "order.status-changed"),
            events.LastPublish);
    }

    private sealed record AuthenticateRequest(string PlayerId);

    private sealed record AuthenticateReply(string PlayerId);

    private sealed record AllocateRoom(string PlayerId);

    private sealed record RoomAllocated(string RoomId);

    private sealed record RoomEvent(string State);

    private sealed record PlaceOrder(string OrderId, string AccountId, long AmountMinor);

    private sealed record OrderPlaced(string OrderId);

    private sealed record ReserveStock(string OrderId, string Sku, int Quantity);

    private sealed record OrderStatusChanged(string OrderId, string Status);

    private sealed class ExampleClient : IZLinkChannelClient
    {
        public string? LastChannelName { get; private set; }

        public string? LastPacketName { get; private set; }

        public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
        {
            LastChannelName = channelName;
            return new ExampleSendCall(packetName => LastPacketName = packetName);
        }

        public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
        {
            LastChannelName = channelName;
            object? reply = request switch
            {
                AuthenticateRequest authenticate => new AuthenticateReply(authenticate.PlayerId),
                PlaceOrder order => new OrderPlaced(order.OrderId),
                _ => null,
            };
            return new ExampleRequestCall(packetName => LastPacketName = packetName, reply);
        }
    }

    private sealed class ExampleRouteClient : IZLinkRouteClient
    {
        public string? RouterChannelId { get; private set; }

        public RoutingId TargetNodeRid { get; private set; }

        public IZLinkSendCall Send<TMessage>(
            string routerChannelId,
            RoutingId targetNodeRid,
            TMessage message)
        {
            RouterChannelId = routerChannelId;
            TargetNodeRid = targetNodeRid;
            return new ExampleRouteSendCall();
        }

        public IZLinkRequestCall Request<TRequest>(
            string routerChannelId,
            RoutingId targetNodeRid,
            TRequest request)
        {
            RouterChannelId = routerChannelId;
            TargetNodeRid = targetNodeRid;
            return new ExampleRouteRequestCall(new RoomAllocated("room-1"));
        }
    }

    private sealed class ExampleFanoutPublisher : IZLinkFanoutClient
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

    private class ExampleSendCall(Action<string> setPacketName) : IZLinkSendCall
    {
        public IZLinkSendCall PacketName(string messageName)
        {
            setPacketName(messageName);
            return this;
        }

        public ValueTask Submit(CancellationToken cancellationToken = default) => ValueTask.CompletedTask;
    }

    private class ExampleRequestCall(Action<string> setPacketName, object? reply) : IZLinkRequestCall
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

    private sealed class ExampleRouteSendCall : ExampleSendCall
    {
        public ExampleRouteSendCall() : base(_ => { })
        {
        }
    }

    private sealed class ExampleRouteRequestCall(object reply) : ExampleRequestCall(_ => { }, reply);

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
