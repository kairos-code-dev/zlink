using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace StoreFailure.Server.Consumer;

internal sealed class LocationStoreDelayState
{
    private int _delayMilliseconds;

    public int DelayMilliseconds => Volatile.Read(ref _delayMilliseconds);

    public void SetDelay(TimeSpan delay)
    {
        var milliseconds = (int)Math.Clamp(delay.TotalMilliseconds, 0, 5000);
        Volatile.Write(ref _delayMilliseconds, milliseconds);
    }
}

/// <summary>
/// E2E deployment decorator that injects a configurable per-operation delay
/// in front of the configured public store. All location semantics remain
/// owned by the inner store.
/// </summary>
internal sealed class DelayableLocationStore(
    IZLinkLocationStore inner,
    LocationStoreDelayState delayState,
    IZLinkLocationChangeStampStore? changeStampStore = null) :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore
{
    private async ValueTask WaitDelayAsync(CancellationToken cancellationToken)
    {
        var delay = delayState.DelayMilliseconds;
        if (delay > 0)
        {
            await Task.Delay(delay, cancellationToken);
        }
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateMeshNodeAsync(descriptor, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveMeshNodeAsync(key, owner, cancellationToken);
    }

    public async ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListMeshNodesAsync(meshName, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateSpotAsync(spot, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteStatus> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveSpotAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveSpotAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.UpdateActorAsync(actor, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveActorAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveActorAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId,
        RoutingId nodeRid,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);
    }

    public async ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ListOwnerLeasesAsync(cancellationToken);
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.PrepareActorTransferAsync(request, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.CommitActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ActivateActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.AbortActorTransferAsync(meshName, actorId, transferId, recoveryOwnerId, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string successorOwnerId,
        TimeSpan recoveryLeaseTtl,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.TakeOverActorTransferAsync(meshName, actorId, transferId, successorOwnerId, recoveryLeaseTtl, cancellationToken);
    }

    public async ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        return await inner.ResolveActorTransferAsync(meshName, actorId, cancellationToken);
    }

    public async ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope,
        CancellationToken cancellationToken = default)
    {
        await WaitDelayAsync(cancellationToken);
        if (changeStampStore is null)
            throw new NotSupportedException("The inner store exposes no change stamp.");
        return await changeStampStore.GetChangeStampAsync(scope, cancellationToken);
    }
}
