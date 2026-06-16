using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;

namespace TicTacToe.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = TicTacToeClientArguments.Parse(args);
        await new TicTacToeClientScenario().RunAsync(options);
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
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
        connector.ObserveInbound((observation, _) =>
        {
            Console.WriteLine(
                "stream-inbound sample=TicTacToe client=player kind={0} name={1} seq={2} bytes={3}",
                observation.Kind,
                observation.Name,
                observation.RequestSeq?.Value.ToString() ?? "-",
                observation.PayloadLength);
            return ValueTask.CompletedTask;
        });
        return connector;
    }
}
