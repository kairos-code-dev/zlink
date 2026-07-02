using Systems.Zlink;

namespace DeliveryDispatch.Server.Configuration;

public sealed record SampleTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string DispatchHttpUrl,
    string CourierRouteEndpoint,
    string TrackingRouteEndpoint,
    string CustomerRouteEndpoint,
    string CustomerStreamEndpoint,
    string CustomerSpotRouterEndpoint,
    string CustomerSpotEndpoint,
    RoutingId CustomerSpotNodeRid,
    string CourierStreamEndpoint,
    string CourierSessionSpotRouterEndpoint,
    string CourierSessionSpotEndpoint,
    RoutingId CourierSessionSpotNodeRid,
    string CourierActorNode1RouteEndpoint,
    string CourierActorNode1RouterEndpoint,
    string CourierActorNode1Endpoint,
    RoutingId CourierActorNode1Rid,
    string CourierActorNode2RouteEndpoint,
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
        var courierRoute = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ROUTE")
                           ?? "tcp://127.0.0.1:7395";
        var trackingRoute = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_ROUTE")
                            ?? "tcp://127.0.0.1:7397";
        var customerRoute = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_ROUTE")
                            ?? "tcp://127.0.0.1:7398";
        var customerStream = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_STREAM")
                             ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_STREAM")
                             ?? "tcp://127.0.0.1:7399";
        var customerSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_SPOT_ROUTER")
                                 ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_SPOT_ROUTER")
                                 ?? "tcp://127.0.0.1:7400";
        var customerSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CUSTOMER_SPOT")
                           ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_SPOT")
                           ?? "tcp://127.0.0.1:7401";
        var courierStream = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_STREAM")
                            ?? "tcp://127.0.0.1:7402";
        var courierSessionSpotRouter =
            Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_SESSION_SPOT_ROUTER")
            ?? "tcp://127.0.0.1:7403";
        var courierSessionSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_SESSION_SPOT")
                                 ?? "tcp://127.0.0.1:7404";
        var courierSpotNode1Route = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTE")
                                    ?? "tcp://127.0.0.1:7405";
        var courierSpotNode1Router = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE1_ROUTER")
                                     ?? "tcp://127.0.0.1:7406";
        var courierSpotNode1 = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE1")
                               ?? "tcp://127.0.0.1:7407";
        var courierSpotNode2Route = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTE")
                                    ?? "tcp://127.0.0.1:7408";
        var courierSpotNode2Router = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE2_ROUTER")
                                     ?? "tcp://127.0.0.1:7409";
        var courierSpotNode2 = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ACTOR_NODE2")
                               ?? "tcp://127.0.0.1:7410";

        return new SampleTopology(
            redisEndpoint,
            redisKeyPrefix,
            dispatchHttp,
            courierRoute,
            trackingRoute,
            customerRoute,
            customerStream,
            customerSpotRouter,
            customerSpot,
            RoutingId.From(SampleNames.CustomerSpotNode),
            courierStream,
            courierSessionSpotRouter,
            courierSessionSpot,
            RoutingId.From(SampleNames.CourierSessionSpotNode),
            courierSpotNode1Route,
            courierSpotNode1Router,
            courierSpotNode1,
            RoutingId.From(SampleNames.CourierActorNode1),
            courierSpotNode2Route,
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