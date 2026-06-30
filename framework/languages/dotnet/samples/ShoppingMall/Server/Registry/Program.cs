using ShoppingMall.Server.Configuration;
using Zlink.Framework.AspNetCore;
using Zlink.Samples.Logging;

namespace ShoppingMall.Server.Registry;

internal static class Program
{
    public static async Task Main()
    {
        using var host = Build(SampleTopology.Create());
        await host.RunAsync();
    }

    private static IHost Build(SampleTopology topology)
    {
        var builder = Host.CreateApplicationBuilder();
        SampleLogging.Configure(
            builder.Logging,
            SampleLogging.DirectoryFromEnvironment("SHOPPINGMALL_LOG_DIR"),
            "registry");
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = topology.RegistryPubEndpoint;
            options.RouterEndpoint = topology.RegistryRouterEndpoint;
        });
        return builder.Build();
    }
}
