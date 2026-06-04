package systems.zlink.samples.bingo.shared.configuration;

public final class SampleNames {
    public static final String ApiChannel = "bingo.api";
    public static final String PlayChannel = "bingo.play";
    public static final String StreamNode = "bingo.client.stream";
    public static final String SessionSpotNode = "bingo.session.node";
    public static final String PlayerActorType = "bingo.player";
    public static final String RoomSpotNode = "bingo.room.node";
    public static final String RoomSpotDiscovery = "bingo.rooms";
    public static final String RoomRouteChannel = "bingo.rooms.route";
    public static final String PlayerJoinedPacket = "PlayerJoinedNotify";
    public static final String GameStartedPacket = "BingoGameStartedNotify";
    public static final String NumberDrawnPacket = "BingoNumberDrawnNotify";
    public static final String StatePacket = "BingoStateNotify";
    public static final String GameEndedPacket = "BingoGameEndedNotify";

    private SampleNames() {
    }
}
