using TicTacToe.Server.Api;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;

namespace TicTacToe.Server;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var mode = args.FirstOrDefault(static arg => !arg.StartsWith("--", StringComparison.Ordinal));
        var settings = SampleSettings.Load(args);

        switch (mode)
        {
            case "play":
                using (var play = new PlayServer(settings).Build())
                {
                    await play.RunAsync();
                }
                break;
            case "api":
                await using (var api = new ApiServer(settings).Build())
                {
                    await api.RunAsync();
                }
                break;
            default:
                await Console.Error.WriteLineAsync("Usage: dotnet run -- [play|api] [--config PATH]");
                Environment.ExitCode = 2;
                break;
        }
    }
}
