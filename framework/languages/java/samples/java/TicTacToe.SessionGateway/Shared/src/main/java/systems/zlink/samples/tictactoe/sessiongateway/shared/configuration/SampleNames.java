package systems.zlink.samples.tictactoe.sessiongateway.shared.configuration;

public final class SampleNames {
    public static final String ApiChannel = "tictactoe-session-api";
    public static final String PlayChannel = "tictactoe-session-play";
    public static final String SpotMesh = "tictactoe";
    public static final String PlayRouteChannel = "play-route";
    public static final String PlayNode = "play";
    public static final String SessionRelayNode = "session-relay";
    public static final String SessionRid = "1101";
    public static final String ReconnectSessionRid = "1102";
    public static final String PlayRid = "2202";
    public static final String GatewayStream = "gateway";
    public static final String PlayerActorType = "player";
    public static final String EntrySpotRoutingId = "3301";
    public static final String XActorId = "player-x";
    public static final String OActorId = "player-o";
    public static final String TurnChangedPacket = "TurnChangedNotify";
    public static final String OpponentJoinedPacket = "OpponentJoinedNotify";
    public static final String GameEndedPacket = "GameEndedNotify";

    private SampleNames() {
    }
}
