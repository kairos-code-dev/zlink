namespace TicTacToe.Server.Configuration;

internal static class SampleChannels
{
    public const string Api = "Api";

    public static string Play(int index)
    {
        return index switch
        {
            0 => "PlayA",
            1 => "PlayB",
            _ => $"Play{index + 1}"
        };
    }
}

internal static class SampleTypes
{
    public const string PlayerActor = "player";
    public const string GameSpot = "tictactoe-game";
}

internal static class SampleDefaults
{
    public const string GameName = "tictactoe-game";
    public const int RequiredLevel = 3;
}

internal static class SampleNodes
{
    public const string ClientStream = "client-stream";
    public const string PlaySpot = "play-node";
}

internal static class SampleTopics
{
    public const string PlayerMilestone = "tictactoe.player.milestone";
}

internal static class SampleTimeouts
{
    public static TimeSpan Request { get; } = TimeSpan.FromSeconds(5);
}