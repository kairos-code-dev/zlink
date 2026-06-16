package systems.zlink.samples.supportchat.server.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47201");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47202");
    public static final String ApiChannelEndpoint =
        property("apiChannelEndpoint", "tcp://127.0.0.1:47203");
    public static final String SupportChannelEndpoint =
        property("supportChannelEndpoint", "tcp://127.0.0.1:47204");
    public static final String SessionSpotEndpoint =
        property("sessionSpotEndpoint", "tcp://127.0.0.1:47205");
    public static final String SessionRouterEndpoint =
        property("sessionRouterEndpoint", "tcp://127.0.0.1:47206");
    public static final String SupportSpotEndpoint =
        property("supportSpotEndpoint", "tcp://127.0.0.1:47210");
    public static final String SupportSpotRouterEndpoint =
        property("supportSpotRouterEndpoint", "tcp://127.0.0.1:47211");
    public static final String SessionRouteEndpoint =
        property("sessionRouteEndpoint", "tcp://127.0.0.1:47212");
    public static final String SupportRouteEndpoint =
        property("supportRouteEndpoint", "tcp://127.0.0.1:47213");
    public static final String StreamEndpoint =
        property("streamEndpoint", "tcp://127.0.0.1:47214");
    public static final String SessionRouterRid = "3101";
    public static final String SessionPubRid = "3102";
    public static final String SupportRid = "4202";

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.supportchat." + name, defaultValue);
    }
}
