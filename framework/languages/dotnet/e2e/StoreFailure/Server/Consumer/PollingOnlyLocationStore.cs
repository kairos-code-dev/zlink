using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace StoreFailure.Server.Consumer;

/// <summary>
/// Delegates every required location capability to Redis without exposing the
/// optional change stamp. The scenario therefore verifies polling as the
/// correctness path.
/// </summary>
internal sealed class PollingOnlyLocationStore(ZLinkRedisLocationStore inner)
    : IZLinkLocationStore
{
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

    public ValueTask<ZLinkLocationPage<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
        string meshName,
        ZLinkPageRequest page,
        CancellationToken cancellationToken = default) =>
        inner.ListMeshNodesAsync(meshName, page, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseClaimResult> ClaimOwnerLeaseAsync(
        string ownerId,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default) =>
        inner.ClaimOwnerLeaseAsync(ownerId, leaseTtl, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseReadResult> ReadOwnerLeaseAsync(
        string ownerId,
        CancellationToken cancellationToken = default) =>
        inner.ReadOwnerLeaseAsync(ownerId, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseRenewResult> RenewOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        TimeSpan leaseTtl,
        CancellationToken cancellationToken = default) =>
        inner.RenewOwnerLeaseAsync(token, leaseTtl, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseReleaseResult> ReleaseOwnerLeaseAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken = default) =>
        inner.ReleaseOwnerLeaseAsync(token, cancellationToken);

    public ValueTask<long> RemoveAllByOwnerAsync(
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveAllByOwnerAsync(owner, cancellationToken);

    public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default) =>
        inner.ReadAuthorityAsync(key, cancellationToken);

    public ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        string expectedStoreVersion,
        ZLinkAuthorityMutation mutation,
        CancellationToken cancellationToken = default) =>
        inner.CompareExchangeAuthorityAsync(
            key, expectedStoreVersion, mutation, cancellationToken);

    public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default) =>
        inner.ListAuthoritiesAsync(prefix, cursor, limit, cancellationToken);

    public ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default) =>
        inner.ReserveAsync(request, cancellationToken);

    public ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default) =>
        inner.CommitAsync(reservation, readyPayload, cancellationToken);

    public ValueTask<ZLinkObjectCreationCompleteResult> CompleteCreationAsync(
        ZLinkObjectReservation reservation,
        ZLinkObjectCreationCompletion completion,
        CancellationToken cancellationToken = default) =>
        inner.CompleteCreationAsync(reservation, completion, cancellationToken);

    public ValueTask<ZLinkCreationTerminalReadResult> ReadCreationTerminalAsync(
        ZLinkCreationOperationId operation,
        CancellationToken cancellationToken = default) =>
        inner.ReadCreationTerminalAsync(operation, cancellationToken);

    public ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default) =>
        inner.AbortAsync(reservation, cancellationToken);

    public ValueTask<ZLinkRelocationCapacityReserveResult> ReserveRelocationCapacityAsync(
        ZLinkRelocationCapacityReservationRequest request,
        CancellationToken cancellationToken = default) =>
        inner.ReserveRelocationCapacityAsync(request, cancellationToken);

    public ValueTask<ZLinkRelocationCapacityAbortResult> AbortRelocationCapacityAsync(
        ZLinkRelocationCapacityFence fence,
        CancellationToken cancellationToken = default) =>
        inner.AbortRelocationCapacityAsync(fence, cancellationToken);

    public ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default) =>
        inner.PrepareAggregateAsync(request, cancellationToken);

    public ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default) =>
        inner.CommitAggregateAsync(fence, cancellationToken);

    public ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default) =>
        inner.AbortAggregateAsync(fence, cancellationToken);
}
