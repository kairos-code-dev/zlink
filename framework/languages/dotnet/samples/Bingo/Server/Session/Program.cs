using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Session;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var topology = SampleTopology.Create();

        await SessionServerHostFactory.Build(
                topology,
                topology.Session(ReadNode(args)))
            .RunAsync();
    }

    private static string ReadNode(string[] args)
    {
        var index = Array.IndexOf(args, "--node");
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : "a";
    }
}
