using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.DispatchApi;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await DispatchApiHostFactory.Build(topology).RunAsync();
    }
}
