using Bingo.Shared.Configuration;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Bingo.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        if (args.Length == 0)
        {
            throw new ArgumentException("Usage: --stream-endpoint tcp://HOST:PORT");
        }

        var streamEndpoint = ReadOption(args, "--stream-endpoint")
            ?? throw new ArgumentException("Missing --stream-endpoint.");
        await using var client1 = CreateClient(streamEndpoint);
        await using var client2 = CreateClient(streamEndpoint);

        await new BingoClientScenario().RunAsync(
            client1,
            client2);
        Console.WriteLine("bingo=completed");
    }

    private static IZlinkStreamConnector CreateClient(string streamEndpoint)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
