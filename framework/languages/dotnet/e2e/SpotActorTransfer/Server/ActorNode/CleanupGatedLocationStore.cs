using System.Collections.Concurrent;
using System.Text;
using Zlink.Framework.Contracts.Locations;

namespace SpotActorTransfer.ActorNode;

internal sealed class ActorCleanupGateStore(EvidenceStore evidence)
{
    private readonly ConcurrentDictionary<string, CleanupGate> _gates = new(StringComparer.Ordinal);

    public bool Arm(string actorId, string scenario) =>
        _gates.TryAdd(ActorAuthorityKey(actorId), new CleanupGate(scenario, actorId));

    public bool AllowAttempt(string actorId) =>
        _gates.TryGetValue(ActorAuthorityKey(actorId), out var gate)
        && gate.AllowAttempt.TrySetResult();

    public bool Release(string actorId) =>
        _gates.TryGetValue(ActorAuthorityKey(actorId), out var gate)
        && gate.Release.TrySetResult();

    public bool IsArmed(ZLinkAuthorityKey key) => _gates.ContainsKey(key.Value);

    public async ValueTask WaitBeforeRemoveAsync(
        ZLinkAuthorityKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        if (!_gates.TryGetValue(key.Value, out var gate)) return;

        await gate.AllowAttempt.Task.WaitAsync(cancellationToken);
        if (Interlocked.Exchange(ref gate.AttemptObserved, 1) == 0)
            evidence.Add(gate.Scenario, gate.ActorId, "source_cleanup_attempt",
                $"owner={owner.OwnerId};generation={owner.Generation}");

        await gate.Release.Task.WaitAsync(cancellationToken);
    }

    public void RecordCompleted(
        ZLinkAuthorityKey key,
        ZLinkLocationOwnerToken owner,
        ZLinkAuthorityCompareExchangeResult result)
    {
        if (!_gates.TryRemove(key.Value, out var gate)) return;

        evidence.Add(gate.Scenario, gate.ActorId, "source_cleanup_completed",
            $"owner={owner.OwnerId};generation={owner.Generation};status={result.GetType().Name}");
    }

    private static string ActorAuthorityKey(string actorId)
    {
        var bytes = new UTF8Encoding(false, true).GetBytes(actorId);
        var builder = new StringBuilder($"zla1:a:{bytes.Length}:");
        foreach (var item in bytes)
        {
            if (item is >= (byte)'A' and <= (byte)'Z'
                or >= (byte)'a' and <= (byte)'z'
                or >= (byte)'0' and <= (byte)'9'
                or (byte)'-' or (byte)'.' or (byte)'_' or (byte)'~')
                builder.Append((char)item);
            else
                builder.Append('%').Append(item.ToString("X2"));
        }
        return builder.ToString();
    }

    private sealed class CleanupGate(string scenario, string actorId)
    {
        public string Scenario { get; } = scenario;
        public string ActorId { get; } = actorId;
        public TaskCompletionSource AllowAttempt { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public int AttemptObserved;
    }
}

/// <summary>
/// E2E deployment decorator that delays only an explicitly armed actor cleanup.
/// All location semantics remain owned by the configured public store.
/// </summary>
internal sealed class CleanupGatedLocationStore(
    IZLinkLocationStore inner,
    ActorCleanupGateStore cleanupGates,
    EvidenceStore evidence) :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
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

    public async ValueTask<ZLinkAuthorityCompareExchangeResult> CompareExchangeAuthorityAsync(
        ZLinkAuthorityKey key,
        string expectedStoreVersion,
        ZLinkAuthorityMutation mutation,
        CancellationToken cancellationToken = default)
    {
        var owner = new ZLinkLocationOwnerToken(string.Empty, 0L);
        var gatedDelete = mutation is ZLinkAuthorityMutation.Delete
                          && cleanupGates.IsArmed(key);
        if (gatedDelete)
        {
            if (await inner.ReadAuthorityAsync(key, cancellationToken)
                    is ZLinkAuthorityReadResult.Found found)
                owner = new ZLinkLocationOwnerToken(
                    found.Snapshot.OwnerId,
                    found.Snapshot.OwnerLeaseGeneration);
            await cleanupGates.WaitBeforeRemoveAsync(key, owner, cancellationToken);
        }

        var result = await inner.CompareExchangeAuthorityAsync(
            key, expectedStoreVersion, mutation, cancellationToken);
        if (gatedDelete)
            cleanupGates.RecordCompleted(key, owner, result);
        return result;
    }

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

    public ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope, CancellationToken cancellationToken = default) =>
        ((IZLinkLocationChangeStampStore)inner).GetChangeStampAsync(scope, cancellationToken);

    public ValueTask DisposeAsync() =>
        inner is IAsyncDisposable disposable ? disposable.DisposeAsync() : ValueTask.CompletedTask;
}
