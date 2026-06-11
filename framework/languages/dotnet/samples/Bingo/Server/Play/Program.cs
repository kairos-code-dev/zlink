using Bingo.Server.Play;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var topology = SampleTopology.Create();
        using var host = PlayServerHostFactory.Build(topology);

        await host.RunAsync();
    }
}
