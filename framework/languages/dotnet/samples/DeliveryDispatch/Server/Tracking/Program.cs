using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Tracking;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await TrackingServerHostFactory.Build(topology).RunAsync();
    }
}
