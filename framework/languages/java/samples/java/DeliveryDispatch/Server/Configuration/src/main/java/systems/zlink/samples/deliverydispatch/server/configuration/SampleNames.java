package systems.zlink.samples.deliverydispatch.server.configuration;

public final class SampleNames {
    public static final String DispatchChannel = "deliverydispatch.dispatch";
    public static final String CourierAChannel = "deliverydispatch.courier.a";
    public static final String CourierBChannel = "deliverydispatch.courier.b";
    public static final String TrackingChannel = "deliverydispatch.tracking";
    public static final String StatusFanoutChannel = "deliverydispatch.status";
    public static final String SpotRouteChannel = "deliverydispatch.spot-route";
    public static final String DeliverySpotDiscovery = "delivery-spots";
    public static final String TrackingSpotNode = "delivery-tracking-node";
    public static final String SessionSpotNode = "delivery-session-node";
    public static final String CustomerStreamNode = "delivery-customer-stream";
    public static final String CustomerActorType = "delivery-customer";

    public static final String CourierA = "courier-a";
    public static final String CourierB = "courier-b";

    private SampleNames() {
    }

    public static String courierChannel(String courierId) {
        return switch (courierId) {
            case CourierA -> CourierAChannel;
            case CourierB -> CourierBChannel;
            default -> throw new IllegalArgumentException("Unknown courier '" + courierId + "'.");
        };
    }
}
