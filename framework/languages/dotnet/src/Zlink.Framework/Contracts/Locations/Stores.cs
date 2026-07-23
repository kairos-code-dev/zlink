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
    IZLinkMeshNodeLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkActorTransferStore,
    IZLinkAuthorityStore
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

public interface IZLinkMeshNodeLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns every descriptor of the mesh as one store snapshot; descriptor
    /// lists are bounded per mesh by contract and never paginated. Returned
    /// descriptors are not ready peers until transport admission completes.
    /// </summary>
    ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);
}

public interface IZLinkInstanceSpotLocationStore
{
    ValueTask<InstanceSpotClaimResult> ClaimInstanceSpotAsync(
        InstanceSpotClaimRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<InstanceSpotWriteResult> CommitInstanceSpotReadyAsync(
        InstanceSpotFence fence,
        ulong spotGeneration,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteResult> BeginInstanceSpotClosingAsync(
        InstanceSpotFence fence,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> ReleaseInstanceSpotAsync(
        InstanceSpotFence fence,
        CancellationToken cancellationToken = default);

    ValueTask<InstanceSpotResolveResult> ResolveInstanceSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Holds one lease row per framework runtime instance. Must share the same
/// physical storage as the location stores so that NewClaim can judge
/// "row owner's lease expired" atomically.
/// </summary>
public interface IZLinkOwnerLeaseStore
{
    async ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        var renewed = await RenewOwnerLeaseAsync(
            ownerId,
            default,
            leaseTtl,
            cancellationToken).ConfigureAwait(false);
        return new ZLinkOwnerLeaseClaimResult.Claimed(
            new ZLinkLocationOwnerToken(ownerId, 1),
            renewed.LeaseExpiresAt,
            renewed.StoreNow);
    }

    async ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        var snapshot = await ListOwnerLeasesAsync(cancellationToken)
            .ConfigureAwait(false);
        var lease = snapshot.Leases.FirstOrDefault(value =>
            string.Equals(value.OwnerId, ownerId, StringComparison.Ordinal));
        return lease is null || lease.LeaseExpiresAt <= snapshot.StoreNow
            ? new ZLinkOwnerLeaseReadResult.Missing()
            : new ZLinkOwnerLeaseReadResult.Found(
                new ZLinkLocationOwnerToken(
                    ownerId,
                    lease.LeaseGeneration == 0
                        ? 1
                        : lease.LeaseGeneration),
                lease.LeaseExpiresAt,
                snapshot.StoreNow);
    }

    async ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        var renewed = await RenewOwnerLeaseAsync(
            token.OwnerId,
            default,
            leaseTtl,
            cancellationToken).ConfigureAwait(false);
        return new ZLinkOwnerLeaseRenewResult.Renewed(
            renewed.LeaseExpiresAt,
            renewed.StoreNow);
    }

    async ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        return await RemoveOwnerLeaseAsync(token.OwnerId, cancellationToken)
            .ConfigureAwait(false)
            ? ZLinkOwnerLeaseReleaseResult.Released
            : ZLinkOwnerLeaseReleaseResult.Stale;
    }

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
/// Optional. Returns a counter the store increments on every row change in
/// the scope, so a polling tick whose stamp is unchanged can skip the full
/// list query. The stamp is an optimization, never a correctness authority
/// (06-location-store §7).
/// </summary>
public interface IZLinkLocationChangeStampStore
{
    ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default);
}
