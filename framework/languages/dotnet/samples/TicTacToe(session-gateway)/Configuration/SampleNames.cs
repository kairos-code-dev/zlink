namespace TicTacToe.SessionGateway.Configuration;

internal static class SampleNames
{
    public const string RouterChannel = "tictactoe.gateway";

    public const string StreamNode = "client.stream";

    public const string XPlayerId = "player-x";

    public const string OPlayerId = "player-o";

    public const string GameId = "sample-game";

    public const string GameStatePacket = nameof(Contracts.GameStateNotify);

    public const string PlayerJoinedPacket = nameof(Contracts.PlayerJoinedNotify);
}

internal static class SampleTimings
{
    public static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(3);

    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(3);
}
