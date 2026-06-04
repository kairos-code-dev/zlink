package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:19181");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:19182");
    public static final String PlayRouteEndpoint =
        property("playRouteEndpoint", "tcp://127.0.0.1:47420");
    public static final String ApiEndpoint =
        property("apiEndpoint", "tcp://127.0.0.1:47403");
    public static final String PlayEndpoint =
        property("playEndpoint", "tcp://127.0.0.1:47404");
    public static final String SessionEndpoint =
        property("sessionEndpoint", "tcp://127.0.0.1:47412");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty(
            "zlink.samples.tictactoe.sessiongateway." + name,
            defaultValue);
    }
}
