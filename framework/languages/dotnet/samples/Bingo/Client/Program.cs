using Bingo.Client.Configuration;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Codecs.Protobuf;

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
        await using var client1 = CreateClient(streamEndpoint, "player1");
        await using var client2 = CreateClient(streamEndpoint, "player2");

        await new BingoClientScenario().RunAsync(
            client1,
            client2);
        Console.WriteLine("bingo=completed");
    }

    private static IZlinkStreamConnector CreateClient(string streamEndpoint, string clientName)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            PayloadCodec = ZLinkProtobufCodec.Default,
        });
        connector.ObserveInbound((observation, _) =>
        {
            Console.WriteLine(
                "stream-inbound sample=Bingo client={0} kind={1} name={2} seq={3} bytes={4}",
                clientName,
                observation.Kind,
                observation.Name,
                observation.RequestSeq?.Value.ToString() ?? "-",
                observation.PayloadLength);
            return ValueTask.CompletedTask;
        });
        return connector;
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
