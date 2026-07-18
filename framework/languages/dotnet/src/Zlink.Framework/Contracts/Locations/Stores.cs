namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// One physical location store providing every required store role. The
/// roles are all-or-nothing on one backend; optional contracts (change
/// stamp, watch) are recognized when the same instance implements them.
/// Register an instance with AddLocationStore — the framework never names a
/// concrete backend on its own surface. The Actor transfer authority
/// (<see cref="IZLinkActorTransferStore"/>) shares the same physical store so
/// that a transfer's participant CAS and the actor location row are fenced by
/// one clock (spec server/languages/dotnet/06-location-store §5).
///
/// Expected races are represented by write status values. Store failures
/// are reported as exceptions for both reads and writes.
/// </summary>
public interface IZLinkLocationStore :
    IZLinkPeerLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkRouteLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkActorTransferStore
{
    /// <summary>
    /// Removes every location row left by an owner, regardless of kind.
    /// Runtime shutdown and takeover cleanup use this path; implementations
    /// should make it one atomic operation when their backend allows it.
    /// </summary>
    ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default);
}

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
    ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Removes a lease. Intended for the owner's own shutdown path and for
    /// operational recovery tools only.
    /// </summary>
    ValueTask<bool> RemoveOwnerLeaseAsync(
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
/// Optional change notification capability. Notifications can make location
/// changes visible before the next polling interval. Event loss is tolerated:
/// polling remains the correctness path.
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
