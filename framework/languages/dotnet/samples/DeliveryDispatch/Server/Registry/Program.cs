using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Registry;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await RegistryHostFactory.Build(topology).RunAsync();
    }
}
