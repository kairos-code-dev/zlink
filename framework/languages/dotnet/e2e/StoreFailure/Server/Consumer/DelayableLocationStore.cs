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
/// Injects the configured delay before every public location-store operation.
/// The inner store continues to own all location semantics.
/// </summary>
internal sealed class DelayableLocationStore(
    IZLinkLocationStore inner,
    LocationStoreDelayState delayState) :
    IZLinkLocationStore
{
    private async ValueTask DelayAsync(CancellationToken cancellationToken)
    {
        var delay = delayState.DelayMilliseconds;
        if (delay > 0)
            await Task.Delay(delay, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.UpdateMeshNodeAsync(descriptor, intent, cancellationToken);
    }

    public async ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
        ZLinkMeshNodeDescriptorKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.RemoveMeshNodeAsync(key, owner, cancellationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ListMeshNodesAsync(meshName, page, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ClaimOwnerLeaseAsync(ownerId, leaseTtl, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReadOwnerLeaseAsync(ownerId, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.RenewOwnerLeaseAsync(token, leaseTtl, cancellationToken);
    }

    public async ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReleaseOwnerLeaseAsync(token, cancellationToken);
    }

    public async ValueTask<long> RemoveAllByOwnerAsync(
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.RemoveAllByOwnerAsync(owner, cancellationToken);
    }

    public async ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReadAuthorityAsync(key, cancellationToken);
    }

    public async ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        string expectedStoreVersion,
        ZLinkAuthorityMutation mutation,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.CompareExchangeAuthorityAsync(
            key, expectedStoreVersion, mutation, cancellationToken);
    }

    public async ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ListAuthoritiesAsync(prefix, cursor, limit, cancellationToken);
    }

    public async ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReserveAsync(request, cancellationToken);
    }

    public async ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.CommitAsync(reservation, readyPayload, cancellationToken);
    }

    public async ValueTask<ZLinkObjectCreationCompleteResult> CompleteCreationAsync(
        ZLinkObjectReservation reservation,
        ZLinkObjectCreationCompletion completion,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.CompleteCreationAsync(reservation, completion, cancellationToken);
    }

    public async ValueTask<ZLinkCreationTerminalReadResult> ReadCreationTerminalAsync(
        ZLinkCreationOperationId operation,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReadCreationTerminalAsync(operation, cancellationToken);
    }

    public async ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.AbortAsync(reservation, cancellationToken);
    }

    public async ValueTask<ZLinkRelocationCapacityReserveResult> ReserveRelocationCapacityAsync(
        ZLinkRelocationCapacityReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.ReserveRelocationCapacityAsync(request, cancellationToken);
    }

    public async ValueTask<ZLinkRelocationCapacityAbortResult> AbortRelocationCapacityAsync(
        ZLinkRelocationCapacityFence fence,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.AbortRelocationCapacityAsync(fence, cancellationToken);
    }

    public async ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.PrepareAggregateAsync(request, cancellationToken);
    }

    public async ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.CommitAggregateAsync(fence, cancellationToken);
    }

    public async ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.AbortAggregateAsync(fence, cancellationToken);
    }

    public async ValueTask<ulong?> GetMeshNodeChangeStampAsync(
        string meshName,
        CancellationToken cancellationToken = default)
    {
        await DelayAsync(cancellationToken);
        return await inner.GetMeshNodeChangeStampAsync(meshName, cancellationToken);
    }
}
