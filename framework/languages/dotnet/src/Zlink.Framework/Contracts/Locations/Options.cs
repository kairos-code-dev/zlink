namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Location runtime policy options. Store implementations never read these;
/// the framework applies cache, heartbeat, and polling policy on top of the
/// registered stores. Defaults follow the draft contract candidates.
/// </summary>
public sealed class ZLinkLocationOptions
{
    public bool PeerCacheEnabled { get; set; } = true;

    public bool SpotCacheEnabled { get; set; } = true;

    public bool ActorCacheEnabled { get; set; } = true;

    public bool RouteCacheEnabled { get; set; } = true;

    /// <summary>Successful single-resolve and peer list cache lifetime.
    /// Not-found results are never cached.</summary>
    public TimeSpan PositiveCacheTtl { get; set; } = TimeSpan.FromMilliseconds(1000);

    /// <summary>Upper bound for cached entries per kind. Sized for this
    /// node's working set, not for the total row count.</summary>
    public int CacheMaxEntries { get; set; } = 4096;

    /// <summary>Owner lease renewal period. One write per runtime instance
    /// per interval; location rows are never written by heartbeat.</summary>
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(5);

    /// <summary>Owner lease lifetime. Rows of an expired owner are treated
    /// as stale everywhere.</summary>
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(15);

    /// <summary>Store re-read period when the store has no watch support.
    /// Also bounds the staleness of the cached owner lease snapshot.</summary>
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);

    /// <summary>Default page size used when a list query passes a default
    /// <see cref="ZLinkPageRequest"/>.</summary>
    public int ListPageSize { get; set; } = 1000;

    /// <summary>How long auto connect keeps the last known desired target
    /// set while the store is unreachable (fail-static).</summary>
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);
}
