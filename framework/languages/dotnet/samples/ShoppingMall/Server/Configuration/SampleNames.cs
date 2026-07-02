using Systems.Zlink;

namespace ShoppingMall.Server.Configuration;

public static class SampleNames
{
    public const string OrderSpotDiscovery = "shoppingmall.order.spot";
    public const string OrderSpotNode = "shoppingmall.order.node";
    public const string OrderWorkflowRouteChannel = "shoppingmall.order.workflow.route";
}

public static class OrderStatuses
{
    public const string Created = "Created";
    public const string InventoryReserved = "InventoryReserved";
    public const string PaymentAuthorized = "PaymentAuthorized";
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
    string ApiARouteEndpoint,
    string ApiBRouteEndpoint,
    string WorkflowAHttpUrl,
    string WorkflowBHttpUrl,
    string WorkflowARouteEndpoint,
    string WorkflowBRouteEndpoint,
    string WorkflowASpotEndpoint,
    string WorkflowASpotRouterEndpoint,
    string WorkflowBSpotEndpoint,
    string WorkflowBSpotRouterEndpoint,
    string StoreDirectory,
    RoutingId ApiARouteRid,
    RoutingId ApiBRouteRid,
    RoutingId WorkflowARouteRid,
    RoutingId WorkflowBRouteRid,
    RoutingId WorkflowASpotRid,
    RoutingId WorkflowBSpotRid)
{
    public static SampleTopology Create()
    {
        return new SampleTopology(
            Read("SHOPPINGMALL_REDIS_ENDPOINT", "127.0.0.1:6379"),
            Read("SHOPPINGMALL_REDIS_KEY_PREFIX", "shoppingmall:"),
            Read("SHOPPINGMALL_API_A_HTTP_URL", "http://127.0.0.1:48203"),
            Read("SHOPPINGMALL_API_B_HTTP_URL", "http://127.0.0.1:48204"),
            Read("SHOPPINGMALL_API_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48205"),
            Read("SHOPPINGMALL_API_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48206"),
            Read("SHOPPINGMALL_WORKFLOW_A_HTTP_URL", "http://127.0.0.1:48207"),
            Read("SHOPPINGMALL_WORKFLOW_B_HTTP_URL", "http://127.0.0.1:48208"),
            Read("SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT", "tcp://127.0.0.1:48209"),
            Read("SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT", "tcp://127.0.0.1:48210"),
            Read("SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT", "tcp://127.0.0.1:48211"),
            Read("SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:48212"),
            Read("SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT", "tcp://127.0.0.1:48213"),
            Read("SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:48214"),
            Read("SHOPPINGMALL_STORE_DIR", Path.Combine(Path.GetTempPath(), "shoppingmall-order-store")),
            RoutingId.From("6001"),
            RoutingId.From("6002"),
            RoutingId.From("6201"),
            RoutingId.From("6202"),
            RoutingId.From("6101"),
            RoutingId.From("6102"));
    }

    public ApiInstanceTopology ForInstance(string instanceId)
    {
        return string.Equals(instanceId, "api-b", StringComparison.Ordinal)
            ? new ApiInstanceTopology(instanceId, ApiBHttpUrl, ApiBRouteEndpoint, ApiBRouteRid)
            : new ApiInstanceTopology("api-a", ApiAHttpUrl, ApiARouteEndpoint, ApiARouteRid);
    }

    public WorkflowInstanceTopology ForWorkflowInstance(string instanceId)
    {
        return string.Equals(instanceId, "workflow-b", StringComparison.Ordinal)
            ? new WorkflowInstanceTopology(
                instanceId,
                WorkflowBHttpUrl,
                WorkflowBRouteEndpoint,
                WorkflowBSpotEndpoint,
                WorkflowBSpotRouterEndpoint,
                WorkflowBRouteRid,
                WorkflowBSpotRid,
                OwnerIndex: 1)
            : new WorkflowInstanceTopology(
                "workflow-a",
                WorkflowAHttpUrl,
                WorkflowARouteEndpoint,
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

    private static string Read(string name, string fallback)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? fallback : value;
    }
}

public sealed record ApiInstanceTopology(
    string InstanceId,
    string HttpUrl,
    string RouteEndpoint,
    RoutingId RouteRid);

public sealed record WorkflowInstanceTopology(
    string InstanceId,
    string HttpUrl,
    string RouteEndpoint,
    string SpotEndpoint,
    string SpotRouterEndpoint,
    RoutingId RouteRid,
    RoutingId SpotRid,
    int OwnerIndex);
