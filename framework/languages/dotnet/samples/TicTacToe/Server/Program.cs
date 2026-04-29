using TicTacToe.Server.Configuration;
using TicTacToe.Client;

namespace TicTacToe.Server;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var mode = args.FirstOrDefault(static arg => !arg.StartsWith("--", StringComparison.Ordinal)) ?? "all";
        var settings = SampleSettings.FromArgs(args);

        switch (mode)
        {
            case "all":
                await RunAllAsync(settings);
                break;
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
            case "client":
                await RunClientAsync(settings, CancellationToken.None);
                break;
            default:
                await Console.Error.WriteLineAsync("Usage: dotnet run -- [all|play|api|client] [--api-url URL] [--api-bind URL] [--api-channel-endpoint tcp://HOST:PORT] [--play-channel-endpoint tcp://HOST:PORT] [--play-endpoint tcp://HOST:PORT] [--spot-endpoint tcp://HOST:PORT] [--log-dir DIR]");
                Environment.ExitCode = 2;
                break;
        }
    }

    private static async Task RunAllAsync(SampleSettings settings)
    {
        settings = settings.WithEphemeralDefaults();

        var play = new PlayServer(settings).Build();
        await using var api = new ApiServer(settings).Build();

        await play.StartAsync();
        await api.StartAsync();

        try
        {
            await RunClientAsync(settings, CancellationToken.None);
        }
        finally
        {
            await api.StopAsync();
            await play.StopAsync();
        }
    }

    private static async Task RunClientAsync(SampleSettings settings, CancellationToken cancellationToken)
    {
        var options = TicTacToeClientOptions.CreateDefault() with
        {
            ApiUrl = new Uri(settings.ApiPublicUrl),
        };
        var result = await new TicTacToeClient().RunAsync(options, cancellationToken);
        result.WriteTo(Console.Out);
    }
}
