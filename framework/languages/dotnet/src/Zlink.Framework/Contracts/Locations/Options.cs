namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Location runtime policy options. A resolve observes the current location
/// state. A resolved spot handle retains its logical key and refreshes its
/// address according to the spot-address messaging contract.
/// </summary>
public sealed class ZLinkLocationOptions
{
    private readonly Dictionary<string, string> _spotRouterChannels = new(StringComparer.Ordinal);

    /// <summary>Owner lease renewal period. One write per runtime instance
    /// per interval; location rows are never written by heartbeat.</summary>
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(10);

    /// <summary>Owner lease lifetime. Rows of an expired owner are treated
    /// as stale everywhere.</summary>
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(30);

    /// <summary>Store re-read period when the store has no watch support.
    /// Also bounds the staleness of the local owner lease snapshot.</summary>
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);

    /// <summary>Default page size used when a list query passes a default
    /// <see cref="ZLinkPageRequest"/>.</summary>
    public int ListPageSize { get; set; } = 1000;

    /// <summary>Additional mesh namespaces the operational query enumerates
    /// beyond the ones this host advertises or dials. An observer host (an
    /// ops console watching a mesh it never joins) lists the meshes it wants
    /// location topology for.</summary>
    public IList<string> ObservedMeshNames { get; } = new List<string>();

    /// <summary>Grace boundary for location-state failures. Existing ready
    /// connections remain available; after this time, auto connect does not
    /// start new outbound connections until location state recovers.</summary>
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);

    /// <summary>
    /// Time reserved to stop every socket that uses an allocated routing id before the
    /// owner lease expires. This policy is used only when allocated routing ids are configured.
    /// </summary>
    public TimeSpan RoutingIdFencingMargin { get; set; } = TimeSpan.FromSeconds(5);

    /// <summary>
    /// Maximum time allowed for one owner-lease renewal attempt when allocated routing ids
    /// require a bounded fencing decision.
    /// </summary>
    public TimeSpan OwnerLeaseRenewTimeout { get; set; } = TimeSpan.FromSeconds(3);

    /// <summary>Maps a Spot mesh name from location rows to the route channel
    /// used to reach that mesh. If no mapping is registered, the Spot mesh
    /// name is used as the route channel name.</summary>
    public void MapSpotMeshToRouteChannel(string spotMeshName, string routeChannelName)
    {
        if (string.IsNullOrWhiteSpace(spotMeshName))
            throw new ArgumentException("Spot mesh name must not be empty.", nameof(spotMeshName));
        if (string.IsNullOrWhiteSpace(routeChannelName))
            throw new ArgumentException("Route channel name must not be empty.", nameof(routeChannelName));

        if (_spotRouterChannels.TryGetValue(spotMeshName, out var existing))
        {
            if (!string.Equals(existing, routeChannelName, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Spot mesh '{spotMeshName}' is already mapped to route channel '{existing}'.");
            return;
        }

        _spotRouterChannels.Add(spotMeshName, routeChannelName);
    }

    internal IReadOnlyDictionary<string, string> SpotRouterChannels => _spotRouterChannels;

    internal bool AllocatedRoutingIdsEnabled { get; set; }
}
