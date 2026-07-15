using Systems.Zlink;

namespace DeliveryDispatch.Server.Configuration;

/// <summary>
/// The endpoints as they appear in the configuration file. The runner decides the ports for a run,
/// but it hands them over in a file rather than through the environment
/// (framework/doc/framework/common/sample-e2e-configuration-policy.ko.md §2.2).
/// </summary>
public sealed class SampleTopologyOptions
{
    public string RedisEndpoint { get; set; } = string.Empty;

    public string RedisKeyPrefix { get; set; } = string.Empty;

    public string DispatchHttpUrl { get; set; } = string.Empty;

    public string DispatchChannelEndpoint { get; set; } = string.Empty;

    public string DispatchSpotRouterEndpoint { get; set; } = string.Empty;

    public string TrackingChannelEndpoint { get; set; } = string.Empty;

    public string TrackingSpotRouterEndpoint { get; set; } = string.Empty;

    public string TrackingSpotEndpoint { get; set; } = string.Empty;

    public string CustomerStreamEndpoint { get; set; } = string.Empty;

    public string CustomerSpotRouterEndpoint { get; set; } = string.Empty;

    public string CustomerSpotEndpoint { get; set; } = string.Empty;

    public string CourierStreamEndpoint { get; set; } = string.Empty;

    public string CourierSessionSpotRouterEndpoint { get; set; } = string.Empty;

    public string CourierSessionSpotEndpoint { get; set; } = string.Empty;

    public string CourierActorNode1RouterEndpoint { get; set; } = string.Empty;

    public string CourierActorNode1Endpoint { get; set; } = string.Empty;

    public string CourierActorNode2RouterEndpoint { get; set; } = string.Empty;

    public string CourierActorNode2Endpoint { get; set; } = string.Empty;

    /// <summary>Turns the file's values into the typed topology, failing on the first one that is
    /// missing. Routing ids are names the sample owns, so they are not configuration.</summary>
    public SampleTopology ToTopology(string role, string? nodeRid)
    {
        var dispatch = role == "dispatch";
        var tracking = role == "tracking";
        var customer = role == "customer-gateway";
        var courierSession = role == "courier-session";
        var courierNode1 = role == "courier-actor-node1";
        var courierNode2 = role == "courier-actor-node2";
        if (!dispatch && !tracking && !customer && !courierSession && !courierNode1 && !courierNode2)
            throw new InvalidOperationException($"Unknown DeliveryDispatch server role '{role}'.");
        if ((courierNode1 || courierNode2) && string.IsNullOrWhiteSpace(nodeRid))
            throw new InvalidOperationException("sample.role.nodeRid is required for a courier actor node.");

        return new SampleTopology(
            Required(RedisEndpoint, nameof(RedisEndpoint)),
            Required(RedisKeyPrefix, nameof(RedisKeyPrefix)),
            Select(dispatch, DispatchHttpUrl, nameof(DispatchHttpUrl)),
            Select(dispatch, DispatchChannelEndpoint, nameof(DispatchChannelEndpoint)),
            Select(dispatch, DispatchSpotRouterEndpoint, nameof(DispatchSpotRouterEndpoint)),
            Select(tracking, TrackingChannelEndpoint, nameof(TrackingChannelEndpoint)),
            Select(tracking, TrackingSpotRouterEndpoint, nameof(TrackingSpotRouterEndpoint)),
            Select(tracking, TrackingSpotEndpoint, nameof(TrackingSpotEndpoint)),
            Select(customer, CustomerStreamEndpoint, nameof(CustomerStreamEndpoint)),
            Select(customer, CustomerSpotRouterEndpoint, nameof(CustomerSpotRouterEndpoint)),
            Select(customer, CustomerSpotEndpoint, nameof(CustomerSpotEndpoint)),
            RoutingId.From(SampleNames.CustomerSpotNode),
            Select(courierSession, CourierStreamEndpoint, nameof(CourierStreamEndpoint)),
            Select(courierSession, CourierSessionSpotRouterEndpoint, nameof(CourierSessionSpotRouterEndpoint)),
            Select(courierSession, CourierSessionSpotEndpoint, nameof(CourierSessionSpotEndpoint)),
            RoutingId.From(SampleNames.CourierSessionSpotNode),
            Select(courierNode1, CourierActorNode1RouterEndpoint, nameof(CourierActorNode1RouterEndpoint)),
            Select(courierNode1, CourierActorNode1Endpoint, nameof(CourierActorNode1Endpoint)),
            RoutingId.From(SampleNames.CourierActorNode1),
            Select(courierNode2, CourierActorNode2RouterEndpoint, nameof(CourierActorNode2RouterEndpoint)),
            Select(courierNode2, CourierActorNode2Endpoint, nameof(CourierActorNode2Endpoint)),
            RoutingId.From(SampleNames.CourierActorNode2));
    }

    private static string Select(bool required, string value, string name) =>
        required ? Required(value, name) : string.Empty;

    private static string Required(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidOperationException($"sample.topology.{name} is required.");

        return value;
    }
}

public sealed record SampleTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string DispatchHttpUrl,
    string DispatchChannelEndpoint,
    string DispatchSpotRouterEndpoint,
    string TrackingChannelEndpoint,
    string TrackingSpotRouterEndpoint,
    string TrackingSpotEndpoint,
    string CustomerStreamEndpoint,
    string CustomerSpotRouterEndpoint,
    string CustomerSpotEndpoint,
    RoutingId CustomerSpotNodeRid,
    string CourierStreamEndpoint,
    string CourierSessionSpotRouterEndpoint,
    string CourierSessionSpotEndpoint,
    RoutingId CourierSessionSpotNodeRid,
    string CourierActorNode1RouterEndpoint,
    string CourierActorNode1Endpoint,
    RoutingId CourierActorNode1Rid,
    string CourierActorNode2RouterEndpoint,
    string CourierActorNode2Endpoint,
    RoutingId CourierActorNode2Rid)
{
    public CourierActorNodePlacement CourierPlacement(string courierId)
    {
        return courierId switch
        {
            "courier-a" => new CourierActorNodePlacement(CourierActorNode1Rid),
            "courier-b" => new CourierActorNodePlacement(CourierActorNode2Rid),
            _ => throw new InvalidOperationException($"Unknown courier '{courierId}'.")
        };
    }
}

public sealed record CourierActorNodePlacement(
    RoutingId NodeRid);
