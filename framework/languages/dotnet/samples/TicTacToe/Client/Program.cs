
namespace TicTacToe.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = TicTacToeClientArguments.Parse(args);
        await new TicTacToeClient().RunAsync(options);
        Console.WriteLine("tictactoe=completed");
    }
}
