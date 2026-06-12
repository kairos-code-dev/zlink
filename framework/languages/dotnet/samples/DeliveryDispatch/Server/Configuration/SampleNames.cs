namespace DeliveryDispatch.Server.Configuration;

public static class SampleNames
{
    public const string DispatchRouteChannel = "deliverydispatch.dispatch";
    public const string CourierAChannel = "deliverydispatch.courier.a";
    public const string CourierBChannel = "deliverydispatch.courier.b";
    public const string TrackingRouteChannel = "deliverydispatch.tracking";
    public const string StatusFanoutChannel = "deliverydispatch.status";
    public const string DeliverySpotDiscovery = "delivery-spots";
    public const string TrackingSpotNode = "delivery-tracking-node";
    public const string SessionSpotNode = "delivery-session-node";
    public const string CustomerStreamNode = "delivery-customer-stream";
    public const string CustomerActorType = "delivery-customer";

    public static string CourierChannel(string courierId)
    {
        return courierId switch
        {
            "courier-a" => CourierAChannel,
            "courier-b" => CourierBChannel,
            _ => throw new InvalidOperationException($"Unknown courier '{courierId}'.")
        };
    }
}

public static class SampleTimings
{
    public static readonly TimeSpan FrameworkTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan DispatchTimeout = TimeSpan.FromMilliseconds(700);
    public static readonly TimeSpan ClientTimeout = TimeSpan.FromSeconds(12);
}
