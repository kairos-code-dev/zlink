using ShoppingMall.Client.Configuration;
using Microsoft.Extensions.Logging;
using Zlink.HttpClient;
using Zlink.Samples.Logging;

namespace ShoppingMall.Client;

internal static class Program
{
    public static async Task Main()
    {
        var topology = SampleTopology.Create();
        using var loggerFactory = SampleLogging.CreateFactory(
            SampleLogging.DirectoryFromEnvironment("SHOPPINGMALL_LOG_DIR"),
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
