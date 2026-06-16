package systems.zlink.samples.deliverydispatch.probe.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47390");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47391");
    public static final String TrackingChannelEndpoint =
        property("trackingChannelEndpoint", "tcp://127.0.0.1:47397");
    public static final String StatusFanoutEndpoint =
        property("statusFanoutEndpoint", "tcp://127.0.0.1:47411");
    public static final String TrackingSpotRouterEndpoint =
        property("trackingSpotRouterEndpoint", "tcp://127.0.0.1:47398");
    public static final String SessionStreamEndpoint =
        property("sessionStreamEndpoint", "tcp://127.0.0.1:47400");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.deliverydispatch." + name, defaultValue);
    }
}
