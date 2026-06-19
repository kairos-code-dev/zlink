using Bingo.Server.Play;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var topology = SampleTopology.Create();
        using var host = PlayServerHostFactory.Build(topology, topology.Play(ReadNode(args)));

        await host.RunAsync();
    }

    private static string ReadNode(string[] args)
    {
        var index = Array.IndexOf(args, "--node");
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : "a";
    }
}
