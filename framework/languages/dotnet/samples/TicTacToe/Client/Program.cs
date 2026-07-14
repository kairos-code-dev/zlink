using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Samples.Logging;

namespace TicTacToe.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var options = TicTacToeClientArguments.Parse(args);
        using var loggerFactory = SampleLogging.CreateFactory(
            TicTacToeClientArguments.ReadLogDirectory(args),
            "client");
        var logger = loggerFactory.CreateLogger("TicTacToe.Client");
        await new TicTacToeClientScenario(logger).RunAsync(options);
        logger.LogInformation("tictactoe=completed");
    }
}

internal static class TicTacToeClientArguments
{
    public static string ReadLogDirectory(string[] args) =>
        ReadOption(args, "--log-dir") ?? "logs";

    public static TicTacToeClientOptions Parse(string[] args)
    {
        var defaults = TicTacToeClientOptions.CreateDefault();
        var apiUrl = ReadOption(args, "--api-url") ?? defaults.ApiUrl.ToString();
        var gameName = ReadOption(args, "--game-name") ?? defaults.GameName;
        var xActorId = ReadOption(args, "--x-actor-id") ?? defaults.XActorId;
        var oActorId = ReadOption(args, "--o-actor-id") ?? defaults.OActorId;
        var observerActorId = ReadOption(args, "--observer-actor-id") ?? defaults.ObserverActorId;

        return defaults with
        {
            ApiUrl = new Uri(apiUrl),
            GameName = gameName,
            XActorId = xActorId,
            OActorId = oActorId,
            ObserverActorId = observerActorId
        };
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        if (index < 0) return null;

        if (index + 1 >= args.Length) throw new ArgumentException($"Missing value for '{name}'.");

        return args[index + 1];
    }
}

public sealed record TicTacToeClientOptions(
    Uri ApiUrl,
    string GameName,
    string XActorId,
    string OActorId,
    string ObserverActorId,
    TimeSpan HttpTimeout,
    TimeSpan StreamTimeout)
{
    public static TicTacToeClientOptions CreateDefault()
    {
        return new TicTacToeClientOptions(
            new Uri("http://127.0.0.1:18080"),
            "tictactoe-game",
            "player-x",
            "player-o",
            "observer",
            TimeSpan.FromSeconds(10),
            TimeSpan.FromSeconds(5));
    }
}

public static class TicTacToeClientConnections
{
    public static IZlinkStreamConnector CreateStreamClient(
        string streamEndpoint,
        TicTacToeClientOptions options,
        string role,
        ILogger logger)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        connector.ObserveInbound((observation, _) =>
        {
            logger.LogInformation(
                "stream-inbound sample=TicTacToe client={0} kind={1} name={2} seq={3} bytes={4}",
                role,
                observation.Kind,
                observation.Name,
                observation.RequestSeq?.ToString() ?? "-",
                observation.PayloadLength);
            return ValueTask.CompletedTask;
        });
        return connector;
    }
}
