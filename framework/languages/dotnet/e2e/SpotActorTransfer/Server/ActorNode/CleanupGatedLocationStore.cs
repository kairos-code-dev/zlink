using System.Collections.Concurrent;
using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace SpotActorTransfer.ActorNode;

internal sealed class ActorCleanupGateStore(EvidenceStore evidence)
{
    private readonly ConcurrentDictionary<string, CleanupGate> _gates = new(StringComparer.Ordinal);

    public bool Arm(string actorId, string scenario) =>
        _gates.TryAdd(actorId, new CleanupGate(scenario));

    public bool AllowAttempt(string actorId) =>
        _gates.TryGetValue(actorId, out var gate) && gate.AllowAttempt.TrySetResult();

    public bool Release(string actorId) =>
        _gates.TryGetValue(actorId, out var gate) && gate.Release.TrySetResult();

    public async ValueTask WaitBeforeRemoveAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken)
    {
        if (!_gates.TryGetValue(key.ActorId, out var gate)) return;

        await gate.AllowAttempt.Task.WaitAsync(cancellationToken);
        if (Interlocked.Exchange(ref gate.AttemptObserved, 1) == 0)
            evidence.Add(gate.Scenario, key.ActorId, "source_cleanup_attempt",
                $"owner={owner.OwnerId};generation={owner.Generation}");

        await gate.Release.Task.WaitAsync(cancellationToken);
    }

    public void RecordCompleted(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner,
        ZLinkLocationWriteStatus status)
    {
        if (!_gates.TryRemove(key.ActorId, out var gate)) return;

        evidence.Add(gate.Scenario, key.ActorId, "source_cleanup_completed",
            $"owner={owner.OwnerId};generation={owner.Generation};status={status}");
    }

    private sealed class CleanupGate(string scenario)
    {
        public string Scenario { get; } = scenario;
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
    ActorCleanupGateStore cleanupGates) :
    IZLinkLocationStore,
    IZLinkLocationChangeStampStore,
    IAsyncDisposable
{
    public async ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
        ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await cleanupGates.WaitBeforeRemoveAsync(key, owner, cancellationToken);
        var status = await inner.RemoveActorAsync(key, owner, cancellationToken);
        cleanupGates.RecordCompleted(key, owner, status);
        return status;
    }

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

    public ValueTask<ulong> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope, CancellationToken cancellationToken = default) =>
        ((IZLinkLocationChangeStampStore)inner).GetChangeStampAsync(scope, cancellationToken);

    public ValueTask DisposeAsync() =>
        inner is IAsyncDisposable disposable ? disposable.DisposeAsync() : ValueTask.CompletedTask;
}
