package systems.zlink.samples.tictactoe.server.configuration;

public final class SampleNames {
    public static final String ApiChannel = "tictactoe-api";
    public static final String PlayChannel = "tictactoe-play";
    public static final String PlayRouteChannel = "tictactoe-router";
    public static final String SpotMesh = "tictactoe";
    public static final String PlayNode = "play";
    public static final String PlayRouterId = "1001";
    public static final String PlayNodeRoutingId = "3200";
    public static final String EntrySpotRoutingId = "3201";
    public static final String PlayStream = "play-stream";
    public static final String PlayActor = "play-actor";
    public static final java.time.Duration RequestTimeout = java.time.Duration.ofSeconds(5);

    private SampleNames() {
    }
}
