namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Provides descriptor, owner lease, and object authority capabilities in
/// one transaction domain.
/// </summary>
public interface IZLinkLocationStore :
    IZLinkMeshNodeLocationStore,
    IZLinkOwnerLeaseStore,
    IZLinkAuthorityStore
{
    /// <summary>
    /// Removes ephemeral descriptors owned by the exact host lease token.
    /// Durable object authority and reservations remain until an explicit
    /// versioned authority operation removes or completes them.
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

    ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        ZLinkPageRequest page,
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
