using SupportChat.Server.Registry;
using SupportChat.Shared.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        await RegistryHostFactory.Build(SampleTopology.Create())
            .RunAsync();
    }
}
