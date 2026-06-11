using ShoppingMallCheckout.Client;
using ShoppingMallCheckout.Client.Configuration;

namespace ShoppingMallCheckout.Client;

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

        await new ShoppingMallCheckoutClientScenario().RunAsync(apiA, apiB, CancellationToken.None);
        Console.WriteLine("shoppingmall=completed");
    }
}
