namespace TicTacToe.Client;

public static class TicTacToeSampleDefaults
{
    public const string ApiUrl = "http://127.0.0.1:18080";
    public const string GameName = "tictactoe-game";
    public const string XPlayerId = "player-x";
    public const string OPlayerId = "player-o";

    public static TimeSpan HttpTimeout { get; } = TimeSpan.FromSeconds(10);

    public static TimeSpan StreamTimeout { get; } = TimeSpan.FromSeconds(5);
}
