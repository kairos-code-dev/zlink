namespace TicTacToe.SessionGateway.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = SessionActorDispatchClientOptions.FromArgs(args);
        var result = await new SessionActorDispatchClient().RunAsync(options);
        result.WriteTo(Console.Out);
    }
}
