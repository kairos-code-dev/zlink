using Bingo.Shared.Contracts;

namespace Bingo.Shared.Configuration;

public static class SampleNames
{
    public const string ApiChannel = "bingo.api";
    public const string PlayChannel = "bingo.play";
    public const string RouterChannel = "bingo.gateway";
    public const string StreamNode = "bingo.client.stream";
    public const string SessionSpotNode = "bingo.session.node";
    public const string PlayerActorType = "bingo.player";
    public const string RoomSpotType = "bingo.room";
    public const string RoomSpotNode = "bingo.room.node";
    public const string RoomSpotDiscovery = "bingo.rooms";

    public static readonly string[] ActorIds =
    [
        "player-1",
        "player-2",
        "player-3",
        "player-4"
    ];

    public const string PlayerJoinedPacket = nameof(PlayerJoinedNotify);
    public const string GameStartedPacket = nameof(BingoGameStartedNotify);
    public const string NumberDrawnPacket = nameof(BingoNumberDrawnNotify);
    public const string StatePacket = nameof(BingoStateNotify);
    public const string GameEndedPacket = nameof(BingoGameEndedNotify);
}

public static class SampleTimings
{
    public static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
    public static readonly TimeSpan DrawPeriod = TimeSpan.FromMilliseconds(20);
}
