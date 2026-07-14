using Zlink.Framework.ContractTests.Support;

namespace Zlink.Framework.ContractTests.Channels;

public sealed class ChannelContracts
{
    [Fact]
    [ContractExample(
        typeof(IZLinkChannelClient),
        typeof(IZLinkSendCall),
        typeof(IZLinkRequestCall),
        typeof(IZLinkRequestCall))]
    public async Task Channel_client_sends_and_requests_by_channel_name()
    {
        var client = new ExampleClient();

        client.SendToChannel("api", new AuthenticateRequest("player-1"))
            .Submit();

        var reply = await client
            .RequestToChannel("api", new AuthenticateRequest("player-1"))
            .Timeout(TimeSpan.FromSeconds(3))
            .Async<AuthenticateReply>();

        Assert.Equal("api", client.LastChannelName);
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

        client.SendToNode("play-router", target, new RoomEvent("opened"))
            .Submit();

        var room = await client
            .RequestToNode("play-router", target, new AllocateRoom("alice"))
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<RoomAllocated>();

        Assert.Equal("play-router", client.RouterChannelId);
        Assert.Equal(target, client.TargetNodeRid);
        Assert.Equal("room-1", room.RoomId);
    }

    [Fact]
    public void Route_request_call_exposes_yield_terminator()
    {
        var methodNames = typeof(IZLinkRequestCall)
            .GetMethods()
            .Select(method => method.Name);

        Assert.Contains("Yield", methodNames);
    }

    [Fact]
    [ContractExample(
        typeof(IZLinkFanoutClient),
        typeof(IZLinkPublishCall))]
    public void Fanout_client_publishes_events_to_a_topic()
    {
        var publisher = new ExampleFanoutPublisher();

        publisher.Publish("events", "room.opened", new RoomEvent("opened"))
            .Submit();

        Assert.Equal(("events", "room.opened"), publisher.LastPublish);
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
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<OrderPlaced>();

        // gRPC unary returning google.protobuf.Empty -> one-way send (no reply awaited).
        orders.SendToChannel("inventory", new ReserveStock("order-1042", "sku-9", 3))
            .Submit();

        // gRPC server-streaming / event feed -> pub/sub fan-out to many subscribers.
        events.Publish("order.events", "order.status", new OrderStatusChanged("order-1042", "Placed"))
            .Submit();

        Assert.Equal("order-1042", placed.OrderId); // unary RPC reply correlated by type
        Assert.Equal("inventory", orders.LastChannelName); // last one-way send routed by channel name
        Assert.Equal(
            ("order.events", "order.status"),
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

        public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
        {
            LastChannelName = channelName;
            return new ExampleSendCall();
        }

        public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
        {
            LastChannelName = channelName;
            object? reply = request switch
            {
                AuthenticateRequest authenticate => new AuthenticateReply(authenticate.PlayerId),
                PlaceOrder order => new OrderPlaced(order.OrderId),
                _ => null
            };
            return new ExampleRequestCall(reply);
        }
    }

    private sealed class ExampleRouteClient : IZLinkRouteClient
    {
        public string? RouterChannelId { get; private set; }

        public RoutingId TargetNodeRid { get; private set; }

        public IZLinkSendCall SendToNode<TMessage>(
            string routerChannelId,
            RoutingId targetNodeRid,
            TMessage message)
        {
            RouterChannelId = routerChannelId;
            TargetNodeRid = targetNodeRid;
            return new ExampleRouteSendCall();
        }

        public IZLinkSendCall SendToSpot<TMessage>(
            SpotHandle target,
            TMessage message)
        {
            return new ExampleRouteSendCall();
        }

        public IZLinkRequestCall RequestToSpot<TRequest>(
            SpotHandle target,
            TRequest request)
        {
            return new ExampleRouteRequestCall(new object());
        }

        public IZLinkRequestCall RequestToNode<TRequest>(
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
        public (string ChannelName, string Topic) LastPublish { get; private set; }

        public IZLinkPublishCall Publish<TEvent>(
            string channelName,
            string topic,
            TEvent message)
        {
            LastPublish = (channelName, topic);
            return new ExamplePublishCall();
        }
    }

    private class ExampleSendCall : IZLinkSendCall
    {
        public void Submit(CancellationToken cancellationToken = default)
        {
        }
    }

    private class ExampleRequestCall(object? reply) : IZLinkRequestCall
    {
        public IZLinkRequestCall Timeout(TimeSpan timeout)
        {
            return this;
        }

        IZLinkRequestCall IZLinkRequestCall.Timeout(TimeSpan timeout)
        {
            return Timeout(timeout);
        }

        public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult((TReply)reply!);
        }

        public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
        {
            return Async<TReply>(cancellationToken);
        }

    }

    private sealed class ExamplePublishCall : IZLinkPublishCall
    {
        public void Submit(CancellationToken cancellationToken = default)
        {
        }
    }

    private sealed class ExampleRouteSendCall : ExampleSendCall
    {
    }

    private sealed class ExampleRouteRequestCall(object reply) : IZLinkRequestCall
    {
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

    private sealed class RoomEventRouteSendHandler : IZLinkRouteSendHandler<RoomEvent>
    {
        public ValueTask HandleAsync(
            RoomEvent message,
            ZLinkRouteSendContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class AllocateRoomRouteHandler : IZLinkRouteRequestHandler<AllocateRoom, RoomAllocated>
    {
        public ValueTask<RoomAllocated> HandleAsync(
            AllocateRoom request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new RoomAllocated("room-1"));
        }
    }
}
