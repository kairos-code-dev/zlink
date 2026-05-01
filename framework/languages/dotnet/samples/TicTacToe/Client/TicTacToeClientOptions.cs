namespace TicTacToe.Client;

public sealed record TicTacToeClientOptions(
    Uri ApiUrl,
    string GameName,
    string XActorId,
    string OActorId,
    TimeSpan HttpTimeout,
    TimeSpan StreamTimeout)
{
    public static TicTacToeClientOptions CreateDefault()
        => new(
            new Uri(TicTacToeSampleDefaults.ApiUrl),
            TicTacToeSampleDefaults.GameName,
            TicTacToeSampleDefaults.XActorId,
            TicTacToeSampleDefaults.OActorId,
            TicTacToeSampleDefaults.HttpTimeout,
            TicTacToeSampleDefaults.StreamTimeout);
}
