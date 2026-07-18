using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace StoreFailure.Server.Consumer;

/// <summary>
/// Delegates everything to the Redis store but does not implement the
/// optional change-stamp surface, so the framework runs on the pure
/// polling path (SF-A2: polling is the correctness path; the stamp is
/// only a latency optimization).
/// </summary>
internal sealed class PollingOnlyLocationStore(ZLinkRedisLocationStore inner) : IZLinkLocationStore
{
    public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveActorAsync(key, owner, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateMeshNodeAsync(descriptor, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveMeshNodeAsync(key, owner, cancellationToken);

    public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default) =>
        inner.ListMeshNodesAsync(meshName, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateSpotAsync(spot, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveSpotAsync(key, owner, cancellationToken);

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default) =>
        inner.ResolveSpotAsync(key, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateActorAsync(actor, intent, cancellationToken);

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default) =>
        inner.ResolveActorAsync(key, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default) =>
        inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);

    public ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default) =>
        inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default) =>
        inner.ListOwnerLeasesAsync(cancellationToken);

    public ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default) =>
        inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default) =>
        inner.PrepareActorTransferAsync(request, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        inner.CommitActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        inner.ActivateActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        inner.AbortActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);

    public ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string successorOwnerId,
        TimeSpan recoveryLeaseTtl,
        CancellationToken cancellationToken = default) =>
        inner.TakeOverActorTransferAsync(meshName, actorId, transferId, successorOwnerId, recoveryLeaseTtl, cancellationToken);

    public ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default) =>
        inner.ResolveActorTransferAsync(meshName, actorId, cancellationToken);
}
