package systems.zlink.samples.deliverydispatch.server.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47390");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47391");
    public static final String ApiHttpUrl =
        property("apiHttpUrl", "http://127.0.0.1:47392");
    public static final String DispatchChannelEndpoint =
        property("dispatchChannelEndpoint", "tcp://127.0.0.1:47394");
    public static final String CourierAEndpoint =
        property("courierAEndpoint", "tcp://127.0.0.1:47395");
    public static final String CourierBEndpoint =
        property("courierBEndpoint", "tcp://127.0.0.1:47396");
    public static final String TrackingChannelEndpoint =
        property("trackingChannelEndpoint", "tcp://127.0.0.1:47397");
    public static final String StatusFanoutEndpoint =
        property("statusFanoutEndpoint", "tcp://127.0.0.1:47411");
    public static final String TrackingSpotRouterEndpoint =
        property("trackingSpotRouterEndpoint", "tcp://127.0.0.1:47398");
    public static final String TrackingSpotEndpoint =
        property("trackingSpotEndpoint", "tcp://127.0.0.1:47399");
    public static final String SessionStreamEndpoint =
        property("sessionStreamEndpoint", "tcp://127.0.0.1:47400");
    public static final String SessionSpotRouterEndpoint =
        property("sessionSpotRouterEndpoint", "tcp://127.0.0.1:47401");
    public static final String SessionSpotEndpoint =
        property("sessionSpotEndpoint", "tcp://127.0.0.1:47402");
    public static final String TrackingSpotRouteEndpoint =
        property("trackingSpotRouteEndpoint", "tcp://127.0.0.1:47403");
    public static final String SessionSpotRouteEndpoint =
        property("sessionSpotRouteEndpoint", "tcp://127.0.0.1:47404");

    public static final String TrackingSpotNodeRid = "7050";
    public static final String SessionSpotNodeRid = "7051";
    public static final String SessionSpotPubRid = "7052";

    private SampleTopology() {
    }

    public static String courierEndpoint(String courierId) {
        return switch (courierId) {
            case SampleNames.CourierA -> CourierAEndpoint;
            case SampleNames.CourierB -> CourierBEndpoint;
            default -> throw new IllegalArgumentException("Unknown courier '" + courierId + "'.");
        };
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.deliverydispatch." + name, defaultValue);
    }
}
