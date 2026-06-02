package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleTopology {
    public static final String ApiEndpoint = "inproc://zlink-java-sample-session-api";
    public static final String SessionEndpoint = "inproc://zlink-java-sample-session-gateway";

    private SampleTopology() {
    }
}
