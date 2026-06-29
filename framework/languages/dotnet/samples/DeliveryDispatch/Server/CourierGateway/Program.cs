using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierGateway;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await CourierGatewayHostFactory.Build(topology).RunAsync();
    }
}
