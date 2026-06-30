using Microsoft.Extensions.Hosting;
using SupportChat.Server.Api;
using SupportChat.Server.Configuration;

internal static class Program
{
    private static async Task Main()
    {
        await ApiServerHostFactory.Build(SampleTopology.Create())
            .RunAsync();
    }
}