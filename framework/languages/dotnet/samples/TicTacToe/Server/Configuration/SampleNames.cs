namespace TicTacToe.Server.Configuration;

internal static class SampleChannels
{
    public const string Api = "Api";
    public const string Play = "Play";
}

internal static class SampleTypes
{
    public const string PlayerActor = "player";
    public const string GameSpot = "tictactoe-game";

    public const string PlaySpotNodeId = "2001";
}

internal static class SampleDefaults
{
    public const string GameName = "tictactoe-game";
}

internal static class SampleNodes
{
    public const string ClientStream = "client-stream";
    public const string PlaySpot = "play-node";
}

internal static class SampleTimeouts
{
    public static TimeSpan Request { get; } = TimeSpan.FromSeconds(5);
}
