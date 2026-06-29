using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierSpot;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var node = ReadOption(args, "--node")
            ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_SPOT_NODE")
            ?? "node1";
        var topology = SampleTopology.Create();
        await CourierSpotHostFactory.Build(topology, node).RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
