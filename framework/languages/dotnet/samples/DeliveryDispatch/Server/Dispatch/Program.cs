using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Dispatch;

internal static class Program
{
    private static async Task Main()
    {
        var topology = SampleTopology.Create();
        await DispatchServerHostFactory.Build(topology).RunAsync();
    }
}
