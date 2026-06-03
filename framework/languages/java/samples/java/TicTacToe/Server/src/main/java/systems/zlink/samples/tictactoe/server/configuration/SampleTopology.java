package systems.zlink.samples.tictactoe.server.configuration;

public final class SampleTopology {
    public static final String ApiEndpoint = "tcp://127.0.0.1:47201";
    public static final String PlayStreamEndpoint = "tcp://127.0.0.1:47202";
    public static final String PlayChannelEndpoint = "tcp://127.0.0.1:47203";
    public static final String PlaySpotRouterEndpoint = "tcp://127.0.0.1:47204";

    private SampleTopology() {
    }
}
