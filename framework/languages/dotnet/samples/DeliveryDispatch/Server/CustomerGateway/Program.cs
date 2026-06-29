using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CustomerGateway;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await CustomerGatewayHostFactory.Build(topology).RunAsync();
    }
}
