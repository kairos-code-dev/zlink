namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Location runtime policy options. Store implementations never read these;
/// the framework applies heartbeat and polling policy on top of the
/// registered stores. Defaults follow the draft contract candidates.
/// Resolvers have no cache: every resolve reads the store, and callers hold
/// resolved spot addresses themselves (spot-address messaging draft).
/// </summary>
public sealed class ZLinkLocationOptions
{
    /// <summary>Owner lease renewal period. One write per runtime instance
    /// per interval; location rows are never written by heartbeat.</summary>
    public TimeSpan HeartbeatInterval { get; set; } = TimeSpan.FromSeconds(5);

    /// <summary>Owner lease lifetime. Rows of an expired owner are treated
    /// as stale everywhere.</summary>
    public TimeSpan OwnerLeaseTtl { get; set; } = TimeSpan.FromSeconds(15);

    /// <summary>Store re-read period when the store has no watch support.
    /// Also bounds the staleness of the local owner lease snapshot.</summary>
    public TimeSpan PollingInterval { get; set; } = TimeSpan.FromSeconds(1);

    /// <summary>Default page size used when a list query passes a default
    /// <see cref="ZLinkPageRequest"/>.</summary>
    public int ListPageSize { get; set; } = 1000;

    /// <summary>Grace boundary for store failure handling. Existing ready
    /// connections stay alive while the transport stays alive; after this
    /// time, auto connect must not start new outbound connects until the
    /// store recovers.</summary>
    public TimeSpan StoreFailureGrace { get; set; } = TimeSpan.FromSeconds(30);
}
