package systems.zlink.samples.bingo.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47101");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47102");
    public static final String ApiChannelEndpoint =
        property("apiChannelEndpoint", "tcp://127.0.0.1:47103");
    public static final String PlayChannelEndpoint =
        property("playChannelEndpoint", "tcp://127.0.0.1:47104");
    public static final String SessionSpotEndpoint =
        property("sessionSpotEndpoint", "tcp://127.0.0.1:47105");
    public static final String SessionRouterEndpoint =
        property("sessionRouterEndpoint", "tcp://127.0.0.1:47106");
    public static final String PlaySpotEndpoint =
        property("playSpotEndpoint", "tcp://127.0.0.1:47110");
    public static final String PlaySpotRouterEndpoint =
        property("playSpotRouterEndpoint", "tcp://127.0.0.1:47111");
    public static final String SessionRouteEndpoint =
        property("sessionRouteEndpoint", "tcp://127.0.0.1:47112");
    public static final String PlayRouteEndpoint =
        property("playRouteEndpoint", "tcp://127.0.0.1:47113");
    public static final String StreamEndpoint =
        property("streamEndpoint", "tcp://127.0.0.1:47114");
    public static final int SessionPort = 29100;
    public static final String SessionRouterRid = "1101";
    public static final String SessionPubRid = "1102";
    public static final String PlayRid = "2202";

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.bingo." + name, defaultValue);
    }
}
