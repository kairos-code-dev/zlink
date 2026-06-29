using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.DispatchCenter;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await DispatchCenterHostFactory.Build(topology).RunAsync();
    }
}
