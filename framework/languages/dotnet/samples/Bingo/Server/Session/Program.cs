using Bingo.Server.Configuration;
using Bingo.Server.Session;
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