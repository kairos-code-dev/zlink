using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.Courier;
using Microsoft.Extensions.Hosting;

internal static class Program
{
    private static async Task Main(string[] args)
    {
        var courierId = ReadOption(args, "--courier")
            ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_ID")
            ?? "courier-a";
        var mode = ReadOption(args, "--mode")
            ?? Environment.GetEnvironmentVariable("DELIVERYDISPATCH_COURIER_MODE")
            ?? "accept";
        var topology = SampleTopology.Create();

        await CourierServerHostFactory.Build(topology, courierId, mode).RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
