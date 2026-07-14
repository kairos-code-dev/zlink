using Bingo.Server.Configuration;
using Bingo.Server.Session;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var configuration = SampleTopology.Load(args);

        await SessionServerHostFactory.Build(
                configuration.Topology,
                configuration.Topology.Session(configuration.NodeName),
                configuration.LogDirectory)
            .RunAsync();
    }
}
