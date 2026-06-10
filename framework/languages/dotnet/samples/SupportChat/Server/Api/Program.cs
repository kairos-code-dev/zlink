using SupportChat.Server.Api;
using SupportChat.Shared.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        await ApiServerHostFactory.Build(SampleTopology.Create())
            .RunAsync();
    }
}
