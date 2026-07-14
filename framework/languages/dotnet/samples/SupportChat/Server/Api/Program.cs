using Microsoft.Extensions.Hosting;
using SupportChat.Server.Api;
using SupportChat.Server.Configuration;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var configuration = SampleTopology.Load(args);
        await ApiServerHostFactory.Build(configuration.Topology, configuration.LogDirectory)
            .RunAsync();
    }
}
