namespace TicTacToe.SessionActorDispatch.Configuration;

internal static class SampleNames
{
    public const string ApiChannel = "tictactoe.api";

    public const string PlayChannel = "tictactoe.play";

    public const string RouterChannel = "tictactoe.gateway";

    public const string StreamNode = "client.stream";

    public const string PlayerActorType = "player";

    public const string XActorId = "player-x";

    public const string OActorId = "player-o";

    public const string MatchId = "sample-game";

    public const string TurnChangedPacket = nameof(Contracts.TurnChangedNotify);

    public const string OpponentJoinedPacket = nameof(Contracts.OpponentJoinedNotify);

    public const string GameEndedPacket = nameof(Contracts.GameEndedNotify);
}

internal static class SampleTimings
{
    public static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(5);

    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
}
