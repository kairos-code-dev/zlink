using Microsoft.Extensions.Configuration;
using Systems.Zlink;

namespace ShoppingMall.Server.Configuration;

public static class SampleNames
{
    public const string MeshName = "shoppingmall";
    public const string OrderWorkflowHandlerGroup = "order-workflow";
    public const string OrderProjectionTopic = "shoppingmall.order.projection";
    public const string OrderProjectionChannel = "shoppingmall.order.projection.channel";

    public static string OrderWorkflowChannelFor(string instanceId)
        => $"shoppingmall.order.workflow.{instanceId}";
}

public static class OrderStatuses
{
    public const string Created = "Created";
    public const string InventoryReserved = "InventoryReserved";
    public const string PaymentAuthorized = "PaymentAuthorized";
    public const string PaymentFailed = "PaymentFailed";
    public const string InventoryReleased = "InventoryReleased";
    public const string Confirmed = "Confirmed";
    public const string Failed = "Failed";
}

public static class SampleTimings
{
    public static readonly TimeSpan HttpTimeout = TimeSpan.FromSeconds(5);
    public static readonly TimeSpan WorkflowTimeout = TimeSpan.FromSeconds(8);
}

public sealed record SampleTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string ApiAHttpUrl,
    string ApiBHttpUrl,
    string WorkflowAHttpUrl,
    string WorkflowBHttpUrl,
    string ApiAMeshEndpoint,
    string ApiBMeshEndpoint,
    string WorkflowAMeshEndpoint,
    string WorkflowBMeshEndpoint,
    RoutingId ApiAMeshRid,
    RoutingId ApiBMeshRid,
    RoutingId WorkflowAMeshRid,
    RoutingId WorkflowBMeshRid)
{
    public static SampleRuntimeConfiguration LoadApi(string[] args) => Load(args, "api");

    public static SampleRuntimeConfiguration LoadWorkflow(string[] args) => Load(args, "workflow");

    private static SampleRuntimeConfiguration Load(string[] args, string role)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");
        var settings = new ConfigurationBuilder()
                           .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
                           .Build()
                           .GetRequiredSection("Sample")
                           .Get<SampleConfiguration>()
                       ?? throw new InvalidOperationException("ShoppingMall Sample configuration is empty.");
        settings.Validate(role);
        var topology = new SampleTopology(
            settings.RedisEndpoint,
            settings.RedisKeyPrefix,
            settings.ApiAHttpUrl,
            settings.ApiBHttpUrl,
            settings.WorkflowAHttpUrl,
            settings.WorkflowBHttpUrl,
            settings.ApiAMeshEndpoint,
            settings.ApiBMeshEndpoint,
            settings.WorkflowAMeshEndpoint,
            settings.WorkflowBMeshEndpoint,
            RoutingId.From("6001"),
            RoutingId.From("6002"),
            RoutingId.From("6101"),
            RoutingId.From("6102"));
        return new SampleRuntimeConfiguration(topology, settings.InstanceId, settings.LogDirectory);
    }

    public ApiInstanceTopology ForInstance(string instanceId)
    {
        return string.Equals(instanceId, "api-b", StringComparison.Ordinal)
            ? new ApiInstanceTopology(instanceId, ApiBHttpUrl, ApiBMeshEndpoint, ApiBMeshRid)
            : new ApiInstanceTopology("api-a", ApiAHttpUrl, ApiAMeshEndpoint, ApiAMeshRid);
    }

    public WorkflowInstanceTopology ForWorkflowInstance(string instanceId)
    {
        return string.Equals(instanceId, "workflow-b", StringComparison.Ordinal)
            ? new WorkflowInstanceTopology(
                instanceId,
                WorkflowBHttpUrl,
                WorkflowBMeshEndpoint,
                WorkflowBMeshRid,
                OwnerIndex: 1)
            : new WorkflowInstanceTopology(
                "workflow-a",
                WorkflowAHttpUrl,
                WorkflowAMeshEndpoint,
                WorkflowAMeshRid,
                OwnerIndex: 0);
    }

    public WorkflowInstanceTopology ForOrderId(string orderId)
    {
        var ownerIndex = StableOwnerIndex(orderId);
        return ownerIndex == 1
            ? ForWorkflowInstance("workflow-b")
            : ForWorkflowInstance("workflow-a");
    }

    private static int StableOwnerIndex(string orderId)
    {
        var sum = 0;
        foreach (var c in orderId)
        {
            sum = (sum * 31 + c) & 0x7fffffff;
        }

        return sum % 2;
    }

}

public sealed record SampleRuntimeConfiguration(
    SampleTopology Topology,
    string InstanceId,
    string LogDirectory);

public sealed class SampleConfiguration
{
    public string InstanceId { get; init; } = "";
    public string LogDirectory { get; init; } = "";
    public string RedisEndpoint { get; init; } = "";
    public string RedisKeyPrefix { get; init; } = "";
    public string ApiAHttpUrl { get; init; } = "";
    public string ApiBHttpUrl { get; init; } = "";
    public string WorkflowAHttpUrl { get; init; } = "";
    public string WorkflowBHttpUrl { get; init; } = "";
    public string ApiAMeshEndpoint { get; init; } = "";
    public string ApiBMeshEndpoint { get; init; } = "";
    public string WorkflowAMeshEndpoint { get; init; } = "";
    public string WorkflowBMeshEndpoint { get; init; } = "";

    public void Validate(string role)
    {
        Require(InstanceId, nameof(InstanceId));
        Require(LogDirectory, nameof(LogDirectory));
        Require(RedisEndpoint, nameof(RedisEndpoint));
        Require(RedisKeyPrefix, nameof(RedisKeyPrefix));
        var suffix = InstanceId.EndsWith("-b", StringComparison.Ordinal) ? "b" : "a";
        if (role == "api")
        {
            Require(suffix == "b" ? ApiBHttpUrl : ApiAHttpUrl,
                suffix == "b" ? nameof(ApiBHttpUrl) : nameof(ApiAHttpUrl));
            Require(suffix == "b" ? ApiBMeshEndpoint : ApiAMeshEndpoint,
                suffix == "b" ? nameof(ApiBMeshEndpoint) : nameof(ApiAMeshEndpoint));
            return;
        }
        if (role != "workflow")
            throw new InvalidOperationException($"Unknown ShoppingMall role '{role}'.");
        if (suffix == "b")
        {
            Require(WorkflowBHttpUrl, nameof(WorkflowBHttpUrl));
            Require(WorkflowBMeshEndpoint, nameof(WorkflowBMeshEndpoint));
        }
        else
        {
            Require(WorkflowAHttpUrl, nameof(WorkflowAHttpUrl));
            Require(WorkflowAMeshEndpoint, nameof(WorkflowAMeshEndpoint));
        }
    }

    private static void Require(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidOperationException($"ShoppingMall Sample.{name} is required.");
    }
}

public sealed record ApiInstanceTopology(
    string InstanceId,
    string HttpUrl,
    string MeshEndpoint,
    RoutingId MeshRid);

public sealed record WorkflowInstanceTopology(
    string InstanceId,
    string HttpUrl,
    string MeshEndpoint,
    RoutingId MeshRid,
    int OwnerIndex);
