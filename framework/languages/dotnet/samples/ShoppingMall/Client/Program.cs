using ShoppingMall.Client;
using ShoppingMall.Client.Configuration;

namespace ShoppingMall.Client;

internal static class Program
{
    public static async Task Main()
    {
        var topology = SampleTopology.Create();
        using var apiA = new HttpClient
        {
            BaseAddress = new Uri(topology.ApiAHttpUrl),
            Timeout = SampleTimings.HttpTimeout,
        };
        using var apiB = new HttpClient
        {
            BaseAddress = new Uri(topology.ApiBHttpUrl),
            Timeout = SampleTimings.HttpTimeout,
        };

        await new ShoppingMallClientScenario().RunAsync(apiA, apiB, CancellationToken.None);
        Console.WriteLine("shoppingmall=completed");
    }
}
