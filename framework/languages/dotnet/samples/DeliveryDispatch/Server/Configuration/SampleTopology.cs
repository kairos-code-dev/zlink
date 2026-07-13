using Systems.Zlink;

namespace DeliveryDispatch.Server.Configuration;

public sealed record SampleTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string DispatchHttpUrl,
    string DispatchChannelEndpoint,
    string DispatchSpotRouterEndpoint,
    string TrackingChannelEndpoint,
    string TrackingSpotRouterEndpoint,
    string TrackingSpotEndpoint,
    string CustomerStreamEndpoint,
    string CustomerSpotRouterEndpoint,
    string CustomerSpotEndpoint,
    RoutingId CustomerSpotNodeRid,
    string CourierStreamEndpoint,
    string CourierSessionSpotRouterEndpoint,
    string CourierSessionSpotEndpoint,
    RoutingId CourierSessionSpotNodeRid,
    string CourierActorNode1RouterEndpoint,
    string CourierActorNode1Endpoint,
    RoutingId CourierActorNode1Rid,
    string CourierActorNode2RouterEndpoint,
    string CourierActorNode2Endpoint,
    RoutingId CourierActorNode2Rid)
{
    public static SampleTopology Create()
    {
        var redisEndpoint = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_REDIS_ENDPOINT")
                            ?? "127.0.0.1:6379";
        var redisKeyPrefix = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_REDIS_KEY_PREFIX")
                             ?? "deliverydispatch:";
        var dispatchHttp = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_DISPATCH_HTTP")
                           ?? "http://127.0.0.1:7392";
        var dispatchChannel = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_DISPATCH_CHANNEL")
                              ?? "tcp://127.0.0.1:7395";
        var dispatchSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_DISPATCH_SPOT_ROUTER")
                                 ?? "tcp://127.0.0.1:7396";
        var trackingChannel = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_CHANNEL")
                              ?? "tcp://127.0.0.1:7397";
        var trackingSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_SPOT_ROUTER")
                                  ?? "tcp://127.0.0.1:7398";
        var trackingSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_SPOT")
                           ?? "tcp://127.0.0.1:7399";
        var customerStream = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_STREAM")
                             ?? "tcp://127.0.0.1:7400";
        var customerSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER")
                                 ?? "tcp://127.0.0.1:7401";
        var customerSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_SPOT")
                           ?? "tcp://127.0.0.1:7402";
        var courierStream = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_STREAM")
                            ?? "tcp://127.0.0.1:7403";
        var courierSessionSpotRouter =
            Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER")
            ?? "tcp://127.0.0.1:7404";
        var courierSessionSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_SESSION_SPOT")
                                 ?? "tcp://127.0.0.1:7405";
        var courierSpotNode1Router = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER")
                                     ?? "tcp://127.0.0.1:7407";
        var courierSpotNode1 = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE1")
                               ?? "tcp://127.0.0.1:7408";
        var courierSpotNode2Router = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER")
                                     ?? "tcp://127.0.0.1:7410";
        var courierSpotNode2 = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE2")
                               ?? "tcp://127.0.0.1:7411";

        return new SampleTopology(
            redisEndpoint,
            redisKeyPrefix,
            dispatchHttp,
            dispatchChannel,
            dispatchSpotRouter,
            trackingChannel,
            trackingSpotRouter,
            trackingSpot,
            customerStream,
            customerSpotRouter,
            customerSpot,
            RoutingId.From(SampleNames.CustomerSpotNode),
            courierStream,
            courierSessionSpotRouter,
            courierSessionSpot,
            RoutingId.From(SampleNames.CourierSessionSpotNode),
            courierSpotNode1Router,
            courierSpotNode1,
            RoutingId.From(SampleNames.CourierActorNode1),
            courierSpotNode2Router,
            courierSpotNode2,
            RoutingId.From(SampleNames.CourierActorNode2));
    }

    public CourierActorNodePlacement CourierPlacement(string courierId)
    {
        return courierId switch
        {
            "courier-a" => new CourierActorNodePlacement(CourierActorNode1Rid),
            "courier-b" => new CourierActorNodePlacement(CourierActorNode2Rid),
            _ => throw new InvalidOperationException($"Unknown courier '{courierId}'.")
        };
    }
}

public sealed record CourierActorNodePlacement(
    RoutingId NodeRid);
