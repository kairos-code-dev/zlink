using ShoppingMall.Client.Configuration;
using Microsoft.Extensions.Logging;
using Zlink.HttpClient;
using Zlink.Samples.Logging;

namespace ShoppingMall.Client;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var configuration = ShoppingMall.Server.Configuration.SampleTopology.Load(args);
        var topology = configuration.Topology;
        using var loggerFactory = SampleLogging.CreateFactory(
            configuration.LogDirectory,
            "client");
        var logger = loggerFactory.CreateLogger("ShoppingMall.Client");
        using var apiA = ZLinkHttpClient.Create(topology.ApiAHttpUrl)
            .Timeout(SampleTimings.HttpTimeout)
            .Build();
        using var apiB = ZLinkHttpClient.Create(topology.ApiBHttpUrl)
            .Timeout(SampleTimings.HttpTimeout)
            .Build();

        await new ShoppingMallClientScenario().RunAsync(apiA, apiB, CancellationToken.None);
        logger.LogInformation("shoppingmall=completed");
    }
}
