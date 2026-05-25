using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Server.Api;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Server.Play.EntrySpot;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Server.Play.Sessions;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Zlink.Framework.Runtime.Core;
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
