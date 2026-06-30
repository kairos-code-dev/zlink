using Microsoft.Extensions.Hosting;
using SupportChat.Server.Configuration;
using SupportChat.Server.Registry;

internal static class Program
{
    private static async Task Main()
    {
        await RegistryHostFactory.Build(SampleTopology.Create())
            .RunAsync();
    }
}