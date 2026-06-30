using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Session;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await SessionServerHostFactory.Build(
                topology,
                topology.PrimarySession)
            .RunAsync();
    }
}