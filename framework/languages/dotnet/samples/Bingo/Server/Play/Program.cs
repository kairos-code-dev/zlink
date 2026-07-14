using Bingo.Server.Configuration;
using Bingo.Server.Play;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var configuration = SampleTopology.Load(args);
        using var host = PlayServerHostFactory.Build(
            configuration.Topology,
            configuration.Topology.Play(configuration.NodeName),
            configuration.LogDirectory);

        await host.RunAsync();
    }

}
