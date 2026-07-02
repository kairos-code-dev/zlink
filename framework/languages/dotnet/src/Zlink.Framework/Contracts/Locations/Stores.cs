namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Stores peer location rows for auto connect. Writes must honor the write
/// intent and owner/generation guard atomically. Read APIs report store
/// failures as exceptions; write APIs return
/// <see cref="ZLinkLocationWriteStatus.StoreUnavailable"/> instead.
/// </summary>
public interface IZLinkPeerLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<long> RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns the peer rows matching the filter as a single snapshot.
    /// Peer lists are bounded to thousands of rows per mesh by contract and
    /// are never paginated; reconcile needs one point-in-time list.
    /// </summary>
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<long> RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<long> RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRouteLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<long> RemoveByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Holds one lease row per framework runtime instance. Must share the same
/// physical storage as the location stores so that NewClaim can judge
/// "row owner's lease expired" atomically.
/// </summary>
public interface IZLinkOwnerLeaseStore
{
    /// <summary>
    /// Upsert: creates the lease row when absent, extends it when present.
    /// Called once per heartbeat interval per runtime instance. The caller
    /// passes a TTL and the store computes the absolute expiry from its own
    /// clock; callers never produce absolute expiry times.
    /// </summary>
    ValueTask<ZLinkLocationWriteResult> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Removes a lease. Intended for the owner's own shutdown path and for
    /// operational recovery tools only.
    /// </summary>
    ValueTask<ZLinkLocationWriteResult> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns every lease plus the store's current time in one snapshot.
    /// Bounded to the number of runtime instances, so never paginated.
    /// </summary>
    ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Optional. When a store implementation also implements this interface the
/// framework wakes reconcile and cache invalidation from change events.
/// Event loss is tolerated: polling remains the correctness path.
/// </summary>
public interface IZLinkLocationWatchStore
{
    IAsyncEnumerable<ZLinkLocationChanged> WatchAsync(
        ZLinkLocationWatchFilter filter,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Optional. Returns a counter the store increments on every row change in
/// the scope, so a polling tick whose stamp is unchanged can skip the full
/// list query.
/// </summary>
public interface IZLinkLocationChangeStampStore
{
    ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}
