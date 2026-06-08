using System.Net.Http.Json;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = TicTacToeClientArguments.Parse(args);
        using var api = new HttpClient
        {
            BaseAddress = options.ApiUrl,
            Timeout = options.HttpTimeout,
        };

        using var createGameResponse = await api.PostAsJsonAsync(
            "/games",
            new CreateGameHttpReq(options.GameName));
        createGameResponse.EnsureSuccessStatusCode();

        var room = await createGameResponse.Content.ReadFromJsonAsync<CreateGameHttpRes>()
                   ?? throw new InvalidOperationException("API returned an empty room response.");
        
        await using var client1 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);
        await using var client2 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);

        await new TicTacToeClientApp().RunAsync(
            room,
            client1,
            client2,
            options);
        Console.WriteLine("tictactoe=completed");
    }
}

internal static class TicTacToeClientArguments
{
    public static TicTacToeClientOptions Parse(string[] args)
    {
        var defaults = TicTacToeClientOptions.CreateDefault();
        var apiUrl = ReadOption(args, "--api-url") ?? defaults.ApiUrl.ToString();
        var gameName = ReadOption(args, "--game-name") ?? defaults.GameName;
        var xActorId = ReadOption(args, "--x-actor-id") ?? defaults.XActorId;
        var oActorId = ReadOption(args, "--o-actor-id") ?? defaults.OActorId;

        return defaults with
        {
            ApiUrl = new Uri(apiUrl),
            GameName = gameName,
            XActorId = xActorId,
            OActorId = oActorId,
        };
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        if (index < 0)
        {
            return null;
        }

        if (index + 1 >= args.Length)
        {
            throw new ArgumentException($"Missing value for '{name}'.");
        }

        return args[index + 1];
    }
}

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
            new Uri("http://127.0.0.1:18080"),
            "tictactoe-game",
            "player-x",
            "player-o",
            TimeSpan.FromSeconds(10),
            TimeSpan.FromSeconds(5));
}

public static class TicTacToeClientConnections
{
    public static IZlinkStreamConnector CreateStreamClient(
        string streamEndpoint,
        TicTacToeClientOptions options)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
    }
}
