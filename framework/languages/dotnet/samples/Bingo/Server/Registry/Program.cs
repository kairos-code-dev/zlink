using Bingo.Server.Registry;
using Bingo.Server.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var topology = SampleTopology.Create();
        await RegistryHostFactory.Build(topology).RunAsync();
    }
}
