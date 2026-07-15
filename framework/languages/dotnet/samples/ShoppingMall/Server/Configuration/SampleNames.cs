using Microsoft.Extensions.Configuration;
using Systems.Zlink;

namespace ShoppingMall.Server.Configuration;

public static class SampleNames
{
    public const string OrderSpotDiscovery = "shoppingmall.order.spot";
    public const string OrderSpotNode = "shoppingmall.order.node";
    public const string OrderWorkflowRouteChannel = "shoppingmall.order.workflow.route";
    public const string OrderProjectionTopic = "shoppingmall.order.projection";

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
    string WorkflowAChannelEndpoint,
    string WorkflowBChannelEndpoint,
    string WorkflowASpotEndpoint,
    string WorkflowASpotRouterEndpoint,
    string WorkflowBSpotEndpoint,
    string WorkflowBSpotRouterEndpoint,
    RoutingId ApiARouteRid,
    RoutingId ApiBRouteRid,
    RoutingId WorkflowARouteRid,
    RoutingId WorkflowBRouteRid,
    RoutingId WorkflowASpotRid,
    RoutingId WorkflowBSpotRid)
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
            settings.WorkflowAChannelEndpoint,
            settings.WorkflowBChannelEndpoint,
            settings.WorkflowASpotEndpoint,
            settings.WorkflowASpotRouterEndpoint,
            settings.WorkflowBSpotEndpoint,
            settings.WorkflowBSpotRouterEndpoint,
            RoutingId.From("6001"),
            RoutingId.From("6002"),
            RoutingId.From("6201"),
            RoutingId.From("6202"),
            RoutingId.From("6101"),
            RoutingId.From("6102"));
        return new SampleRuntimeConfiguration(topology, settings.InstanceId, settings.LogDirectory);
    }

    public ApiInstanceTopology ForInstance(string instanceId)
    {
        return string.Equals(instanceId, "api-b", StringComparison.Ordinal)
            ? new ApiInstanceTopology(instanceId, ApiBHttpUrl, ApiBRouteRid)
            : new ApiInstanceTopology("api-a", ApiAHttpUrl, ApiARouteRid);
    }

    public WorkflowInstanceTopology ForWorkflowInstance(string instanceId)
    {
        return string.Equals(instanceId, "workflow-b", StringComparison.Ordinal)
            ? new WorkflowInstanceTopology(
                instanceId,
                WorkflowBHttpUrl,
                WorkflowBChannelEndpoint,
                WorkflowBSpotEndpoint,
                WorkflowBSpotRouterEndpoint,
                WorkflowBRouteRid,
                WorkflowBSpotRid,
                OwnerIndex: 1)
            : new WorkflowInstanceTopology(
                "workflow-a",
                WorkflowAHttpUrl,
                WorkflowAChannelEndpoint,
                WorkflowASpotEndpoint,
                WorkflowASpotRouterEndpoint,
                WorkflowARouteRid,
                WorkflowASpotRid,
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
    public string WorkflowAChannelEndpoint { get; init; } = "";
    public string WorkflowBChannelEndpoint { get; init; } = "";
    public string WorkflowASpotEndpoint { get; init; } = "";
    public string WorkflowASpotRouterEndpoint { get; init; } = "";
    public string WorkflowBSpotEndpoint { get; init; } = "";
    public string WorkflowBSpotRouterEndpoint { get; init; } = "";

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
            return;
        }
        if (role != "workflow")
            throw new InvalidOperationException($"Unknown ShoppingMall role '{role}'.");
        if (suffix == "b")
        {
            Require(WorkflowBHttpUrl, nameof(WorkflowBHttpUrl));
            Require(WorkflowBChannelEndpoint, nameof(WorkflowBChannelEndpoint));
            Require(WorkflowBSpotEndpoint, nameof(WorkflowBSpotEndpoint));
            Require(WorkflowBSpotRouterEndpoint, nameof(WorkflowBSpotRouterEndpoint));
        }
        else
        {
            Require(WorkflowAHttpUrl, nameof(WorkflowAHttpUrl));
            Require(WorkflowAChannelEndpoint, nameof(WorkflowAChannelEndpoint));
            Require(WorkflowASpotEndpoint, nameof(WorkflowASpotEndpoint));
            Require(WorkflowASpotRouterEndpoint, nameof(WorkflowASpotRouterEndpoint));
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
    RoutingId RouteRid);

public sealed record WorkflowInstanceTopology(
    string InstanceId,
    string HttpUrl,
    string ChannelEndpoint,
    string SpotEndpoint,
    string SpotRouterEndpoint,
    RoutingId RouteRid,
    RoutingId SpotRid,
    int OwnerIndex);
