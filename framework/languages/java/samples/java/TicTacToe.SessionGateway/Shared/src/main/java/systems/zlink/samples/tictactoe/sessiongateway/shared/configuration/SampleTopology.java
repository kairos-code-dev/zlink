package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:19181");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:19182");
    public static final String PlayRouteEndpoint =
        property("playRouteEndpoint", "tcp://127.0.0.1:47420");
    public static final String PlaySpotRouterEndpoint =
        property("playSpotRouterEndpoint", "tcp://127.0.0.1:47421");
    public static final String SessionRouteEndpoint =
        property("sessionRouteEndpoint", "tcp://127.0.0.1:47422");
    public static final String SessionRouterEndpoint =
        property("sessionRouterEndpoint", "tcp://127.0.0.1:47423");
    public static final String ReconnectSessionRouteEndpoint =
        property("reconnectSessionRouteEndpoint", "tcp://127.0.0.1:47424");
    public static final String ReconnectSessionRouterEndpoint =
        property("reconnectSessionRouterEndpoint", "tcp://127.0.0.1:47425");
    public static final String ApiEndpoint =
        property("apiEndpoint", "tcp://127.0.0.1:47403");
    public static final String PlayChannelEndpoint =
        property("playEndpoint", "tcp://127.0.0.1:47404");
    public static final String SessionEndpoint =
        property("sessionEndpoint", "tcp://127.0.0.1:47412");
    public static final String ReconnectSessionEndpoint =
        property("reconnectSessionEndpoint", "tcp://127.0.0.1:47413");

    private SampleTopology() {
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty(
            "zlink.samples.tictactoe.sessiongateway." + name,
            defaultValue);
    }

    public static SampleSessionNode primarySession() {
        return new SampleSessionNode(
            SessionRouteEndpoint,
            SessionRouterEndpoint,
            SessionEndpoint,
            SampleNames.SessionRid);
    }

    public static SampleSessionNode reconnectSession() {
        return new SampleSessionNode(
            ReconnectSessionRouteEndpoint,
            ReconnectSessionRouterEndpoint,
            ReconnectSessionEndpoint,
            SampleNames.ReconnectSessionRid);
    }

    public record SampleSessionNode(
        String routeEndpoint,
        String routerEndpoint,
        String streamEndpoint,
        String routingId) {
    }
}
