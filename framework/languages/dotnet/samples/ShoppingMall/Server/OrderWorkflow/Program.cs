using ShoppingMall.Server.Configuration;

namespace ShoppingMall.Server.OrderWorkflow;

internal static class Program
{
    public static async Task Main(string[] args)
    {
        var instanceId = ReadOption(args, "--instance")
                         ?? Environment.GetEnvironmentVariable("SHOPPINGMALL_WORKFLOW_INSTANCE")
                         ?? "workflow-a";
        var topology = SampleTopology.Create();
        var instance = topology.ForWorkflowInstance(instanceId);
        await using var app = OrderWorkflowServerHostFactory.Build(topology, instance, args);
        await app.RunAsync();
    }

    private static string? ReadOption(string[] args, string name)
    {
        var index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
    }
}
