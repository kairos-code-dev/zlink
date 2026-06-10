using SupportChat.Shared.Configuration;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        await SupportChat.Server.Support.SupportServerHostFactory.Build(SampleTopology.Create())
            .RunAsync();
    }
}
