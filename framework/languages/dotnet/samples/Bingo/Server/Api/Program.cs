using Bingo.Server.Api;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var configuration = SampleTopology.Load(args);
        await ApiServerHostFactory.Build(
                configuration.Topology,
                configuration.Topology.Api(configuration.NodeName),
                configuration.LogDirectory)
            .RunAsync();
    }
}
