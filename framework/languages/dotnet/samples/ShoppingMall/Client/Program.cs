using ShoppingMall.Client;
using ShoppingMall.Client.Configuration;
using Zlink.HttpClient;

namespace ShoppingMall.Client;

internal static class Program
{
    public static async Task Main()
    {
        var topology = SampleTopology.Create();
        using var apiA = ZLinkHttpClient.Create(topology.ApiAHttpUrl)
            .Json()
            .Timeout(SampleTimings.HttpTimeout)
            .Build();
        using var apiB = ZLinkHttpClient.Create(topology.ApiBHttpUrl)
            .Json()
            .Timeout(SampleTimings.HttpTimeout)
            .Build();

        await new ShoppingMallClientScenario().RunAsync(apiA, apiB, CancellationToken.None);
        Console.WriteLine("shoppingmall=completed");
    }
}
