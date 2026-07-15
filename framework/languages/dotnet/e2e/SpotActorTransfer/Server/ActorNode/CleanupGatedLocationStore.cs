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
        ZLinkLocationWriteResult result)
    {
        if (!_gates.TryRemove(key.ActorId, out var gate)) return;

        evidence.Add(gate.Scenario, key.ActorId, "source_cleanup_completed",
            $"owner={owner.OwnerId};generation={owner.Generation};status={result.Status}");
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
    public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
        ZLinkPeerLocation peer, ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdatePeerAsync(peer, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key, ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemovePeerAsync(key, owner, cancellationToken);

    public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter, CancellationToken cancellationToken = default) =>
        inner.ListPeersAsync(filter, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
        ZLinkSpotLocation spot, ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateSpotAsync(spot, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key, ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveSpotAsync(key, owner, cancellationToken);

    public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key, CancellationToken cancellationToken = default) =>
        inner.ResolveSpotAsync(key, cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter, ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        inner.ListSpotsAsync(filter, page, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
        ZLinkActorLocation actor, ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateActorAsync(actor, intent, cancellationToken);

    public async ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default)
    {
        await cleanupGates.WaitBeforeRemoveAsync(key, owner, cancellationToken);
        var result = await inner.RemoveActorAsync(key, owner, cancellationToken);
        cleanupGates.RecordCompleted(key, owner, result);
        return result;
    }

    public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key, CancellationToken cancellationToken = default) =>
        inner.ResolveActorAsync(key, cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter, ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        inner.ListActorsAsync(filter, page, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
        ZLinkRouteLocation route, ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default) =>
        inner.UpdateRouteAsync(route, intent, cancellationToken);

    public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key, ZLinkLocationOwnerToken owner,
        CancellationToken cancellationToken = default) =>
        inner.RemoveRouteAsync(key, owner, cancellationToken);

    public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key, CancellationToken cancellationToken = default) =>
        inner.ResolveRouteAsync(key, cancellationToken);

    public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter, ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default) =>
        inner.ListRoutesAsync(filter, page, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
        string ownerId, RoutingId nodeRid, TimeSpan leaseTtl,
        CancellationToken cancellationToken = default) =>
        inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);

    public ValueTask<bool> RemoveOwnerLeaseAsync(
        string ownerId, CancellationToken cancellationToken = default) =>
        inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

    public ValueTask<long> RemoveAllByOwnerAsync(
        string ownerId, CancellationToken cancellationToken = default) =>
        inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);

    public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
        CancellationToken cancellationToken = default) =>
        inner.ListOwnerLeasesAsync(cancellationToken);

    public ValueTask<long> GetChangeStampAsync(
        ZLinkLocationChangeStampScope scope, CancellationToken cancellationToken = default) =>
        ((IZLinkLocationChangeStampStore)inner).GetChangeStampAsync(scope, cancellationToken);

    public ValueTask DisposeAsync() =>
        inner is IAsyncDisposable disposable ? disposable.DisposeAsync() : ValueTask.CompletedTask;
}
