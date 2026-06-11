using SupportChat.Server.Session;
using SupportChat.Server.Configuration;
using Microsoft.Extensions.Hosting;

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
