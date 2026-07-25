namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Provides descriptor, owner lease, and object authority capabilities in
/// one transaction domain.
/// </summary>
public interface IZLinkLocationStore :
    IZLinkMeshNodeLocationStore,
    IZLinkSpotLocationStore,
    IZLinkActorLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkAuthorityStore
{
    /// <summary>
    /// Removes every location row left by an owner, regardless of kind.
    /// Runtime shutdown and takeover cleanup use this path; implementations
    /// should make it one atomic operation when their backend allows it.
    /// </summary>
    ValueTask<long> RemoveAllByOwnerAsync(
        ZLinkLocationOwnerToken owner,
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

    ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkClientServerLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateClientServerAsync(
        ZLinkClientServerServerDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> RemoveClientServerAsync(
        ZLinkClientServerServerDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkClientServerServerDescriptor>> ListClientServersAsync(
        string channelName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default);
}

public interface IZLinkFanoutLocationStore
{
    ValueTask<ZLinkLocationWriteResult> UpdateFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationWriteStatus> RemoveFanoutPublisherAsync(
        ZLinkFanoutPublisherDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkFanoutPublisherDescriptor>> ListFanoutPublishersAsync(
        string channelName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default);
}

// Legacy operational projections remain temporarily while runtime callers
// move to opaque authority reads. They are not authority for placement.
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

public interface IZLinkOwnerLeaseStore
{
    ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
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
