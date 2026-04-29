
namespace TicTacToe.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = TicTacToeClientArguments.Parse(args);
        var result = await new TicTacToeClient().RunAsync(options);
        result.WriteTo(Console.Out);
    }
}
