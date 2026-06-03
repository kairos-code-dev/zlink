package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint = "tcp://127.0.0.1:19181";
    public static final String RegistryRouterEndpoint = "tcp://127.0.0.1:19182";
    public static final String PlayRouteEndpoint = "inproc://zlink-java-sample-session-play-route";
    public static final String ApiEndpoint = "tcp://127.0.0.1:47403";
    public static final String PlayEndpoint = "tcp://127.0.0.1:47404";
    public static final String SessionEndpoint = "tcp://127.0.0.1:47412";

    private SampleTopology() {
    }
}
