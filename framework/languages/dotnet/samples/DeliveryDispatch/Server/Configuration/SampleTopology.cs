using Systems.Zlink;

namespace DeliveryDispatch.Server.Configuration;

public sealed record SampleTopology(
    string RegistryRouterEndpoint,
    string RegistryPubEndpoint,
    string DispatchApiHttpUrl,
    string DispatchCenterRouteEndpoint,
    RoutingId DispatchCenterRouteRid,
    string CourierAEndpoint,
    string CourierBEndpoint,
    string TrackingRouteEndpoint,
    string StatusFanoutEndpoint,
    string TrackingSpotRouterEndpoint,
    string TrackingSpotEndpoint,
    RoutingId TrackingSpotNodeRid,
    string SessionStreamEndpoint,
    string SessionSpotRouterEndpoint,
    string SessionSpotEndpoint,
    RoutingId SessionSpotNodeRid,
    RoutingId SessionSpotPubRid)
{
    public static SampleTopology Create()
    {
        var registry = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_REGISTRY")
            ?? "tcp://127.0.0.1:7391";
        var registryPub = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_REGISTRY_PUB")
            ?? "tcp://127.0.0.1:7390";
        var apiHttp = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_API_HTTP")
            ?? "http://127.0.0.1:7392";
        var centerRoute = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_CENTER_ROUTE")
            ?? "tcp://127.0.0.1:7394";
        var courierA = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_A_ROUTE")
            ?? "tcp://127.0.0.1:7395";
        var courierB = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_B_ROUTE")
            ?? "tcp://127.0.0.1:7396";
        var trackingRoute = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_ROUTE")
            ?? "tcp://127.0.0.1:7397";
        var statusFanout = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_STATUS_FANOUT")
            ?? "tcp://127.0.0.1:7411";
        var trackingSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_SPOT_ROUTER")
            ?? "tcp://127.0.0.1:7398";
        var trackingSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_TRACKING_SPOT")
            ?? "tcp://127.0.0.1:7399";
        var sessionStream = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_STREAM")
            ?? "tcp://127.0.0.1:7400";
        var sessionSpotRouter = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_SPOT_ROUTER")
            ?? "tcp://127.0.0.1:7401";
        var sessionSpot = Environment.GetEnvironmentVariable("DELIVERYDISPATCH_SESSION_SPOT")
            ?? "tcp://127.0.0.1:7402";

        return new SampleTopology(
            registry,
            registryPub,
            apiHttp,
            centerRoute,
            RoutingId.From("7002"),
            courierA,
            courierB,
            trackingRoute,
            statusFanout,
            trackingSpotRouter,
            trackingSpot,
            RoutingId.From("tracking-spot"),
            sessionStream,
            sessionSpotRouter,
            sessionSpot,
            RoutingId.From("session-spot"),
            RoutingId.From("session-spot-pub"));
    }

    public string CourierEndpoint(string courierId)
    {
        return courierId switch
        {
            "courier-a" => CourierAEndpoint,
            "courier-b" => CourierBEndpoint,
            _ => throw new InvalidOperationException($"Unknown courier '{courierId}'.")
        };
    }

}
