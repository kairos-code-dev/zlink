namespace TicTacToe.Client;

public sealed record TicTacToeClientOptions(
    Uri ApiUrl,
    string GameName,
    string XPlayerId,
    string OPlayerId,
    TimeSpan HttpTimeout,
    TimeSpan StreamTimeout)
{
    public static TicTacToeClientOptions CreateDefault()
        => new(
            new Uri(TicTacToeSampleDefaults.ApiUrl),
            TicTacToeSampleDefaults.GameName,
            TicTacToeSampleDefaults.XPlayerId,
            TicTacToeSampleDefaults.OPlayerId,
            TicTacToeSampleDefaults.HttpTimeout,
            TicTacToeSampleDefaults.StreamTimeout);
}
