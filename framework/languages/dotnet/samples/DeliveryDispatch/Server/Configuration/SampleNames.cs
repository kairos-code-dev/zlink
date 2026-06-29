namespace DeliveryDispatch.Server.Configuration;

public static class SampleNames
{
    public const string CourierRouteChannel = "deliverydispatch.courier";
    public const string CourierSpotRouteChannel = "delivery-couriers.route";
    public const string TrackingRouteChannel = "deliverydispatch.tracking";
    public const string CustomerRouteChannel = "deliverydispatch.customer";
    public const string CustomerActorDiscovery = "delivery-customers";
    public const string CourierActorDiscovery = "delivery-couriers";
    public const string CustomerSpotNode = "delivery-customer-node";
    public const string CourierSessionSpotNode = "delivery-courier-session-node";
    public const string CourierSpotNode1 = "delivery-courier-node-1";
    public const string CourierSpotNode2 = "delivery-courier-node-2";
    public const string CourierEntrySpotNode1 = "delivery-courier-entry-1";
    public const string CourierEntrySpotNode2 = "delivery-courier-entry-2";
    public const string CustomerStreamNode = "delivery-customer-stream";
    public const string CourierStreamNode = "delivery-courier-stream";
    public const string CustomerActorType = "delivery-customer";
    public const string CourierActorType = "delivery-courier";
}

public static class SampleTimings
{
    public static readonly TimeSpan FrameworkTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan DispatchTimeout = TimeSpan.FromMilliseconds(700);
    public static readonly TimeSpan ClientTimeout = TimeSpan.FromSeconds(12);
}
