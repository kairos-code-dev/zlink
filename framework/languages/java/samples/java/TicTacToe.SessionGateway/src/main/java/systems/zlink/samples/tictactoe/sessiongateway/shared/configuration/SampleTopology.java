package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint = "tcp://127.0.0.1:19181";
    public static final String RegistryRouterEndpoint = "tcp://127.0.0.1:19182";
    public static final String PlayRouteEndpoint = "inproc://zlink-java-sample-session-play-route";
    public static final String ApiEndpoint = "inproc://zlink-java-sample-session-api";
    public static final String SessionEndpoint = "inproc://zlink-java-sample-session-gateway";

    private SampleTopology() {
    }
}
