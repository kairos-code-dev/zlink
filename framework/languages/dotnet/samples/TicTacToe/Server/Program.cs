using System.Net.Http.Json;
using TicTacToe.Client;
using TicTacToe.Server.Api;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Shared.Contracts;

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
        using var api = new HttpClient
        {
            BaseAddress = options.ApiUrl,
            Timeout = options.HttpTimeout,
        };

        using var createGameResponse = await api.PostAsJsonAsync(
            "/games",
            new CreateGameHttpReq(options.GameName),
            cancellationToken);
        createGameResponse.EnsureSuccessStatusCode();

        var room = await createGameResponse.Content.ReadFromJsonAsync<CreateGameHttpRes>(cancellationToken)
                   ?? throw new InvalidOperationException("API returned an empty room response.");
        await using var client1 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);
        await using var client2 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);

        await new TicTacToeClientApp().RunAsync(
            room,
            client1,
            client2,
            options,
            cancellationToken);
        Console.WriteLine("tictactoe=completed");
    }
}
