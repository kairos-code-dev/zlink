using GameQuest.Server.Configuration;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Samples.Logging;

namespace GameQuest.Registry;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var topology = GameQuestTopology.FromEnvironment();
        var builder = Host.CreateApplicationBuilder(args);
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("GAMEQUEST_LOG_DIR"),
            "registry");
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = Environment.GetEnvironmentVariable("GAMEQUEST_REGISTRY_PUB_ENDPOINT")
                                  ?? throw new InvalidOperationException(
                                      "GAMEQUEST_REGISTRY_PUB_ENDPOINT is required.");
            options.RouterEndpoint = topology.RegistryRouterEndpoint;
        });
        await builder.Build().RunAsync();
    }
}
