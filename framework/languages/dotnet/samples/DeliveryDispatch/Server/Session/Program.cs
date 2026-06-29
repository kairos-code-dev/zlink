using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Session;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await SessionServerHostFactory.Build(topology).RunAsync();
    }
}
