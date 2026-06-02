package systems.zlink.samples.tictactoe.server.configuration;

public final class SampleTopology {
    public static final String ApiEndpoint = "inproc://zlink-java-sample-tictactoe-api";
    public static final String PlayStreamEndpoint = "inproc://zlink-java-sample-tictactoe-stream";

    private SampleTopology() {
    }
}
