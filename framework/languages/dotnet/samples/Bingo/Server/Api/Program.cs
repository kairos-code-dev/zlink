using Bingo.Server.Api;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var topology = SampleTopology.Create();
        await ApiServerHostFactory.Build(topology, topology.Api(ReadNode(args))).RunAsync();
    }

    private static string ReadNode(string[] args)
    {
        var index = Array.IndexOf(args, "--node");
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : "a";
    }
}
