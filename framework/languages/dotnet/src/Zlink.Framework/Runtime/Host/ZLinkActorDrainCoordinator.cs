using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Host;

/// <summary>
/// Coordinates actor target discovery and managed handoff during runtime drain.
/// </summary>
internal sealed class ZLinkActorDrainCoordinator(
    ZLinkFrameworkActorFacade actors,
    ZLinkActorSessionManager actorSessions,
    IServiceProvider services,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask<ZLinkFrameworkTerminationReason?> PreflightAsync(
        CancellationToken cancellationToken)
    {
        var states = actorSessions.SnapshotStates();
        if (states.Length == 0)
            return null;

        try
        {
            foreach (var actorType in states
                         .Select(static state => state.ActorType)
                         .Where(static actorType => !string.IsNullOrWhiteSpace(actorType))
                         .Distinct(StringComparer.Ordinal))
            {
                var targets = await ResolveTargetsAsync(actorType!, cancellationToken)
                    .ConfigureAwait(false);
                foreach (var state in states.Where(state =>
                             string.Equals(state.ActorType, actorType, StringComparison.Ordinal)))
                {
                    if (state.Actor is null || state.NativeActorRef is not { } actorRef)
                        continue;
                    if (!targets.Any(target => target.NodeRid != actorRef.NodeRid))
                        return ZLinkFrameworkTerminationReason.TargetUnavailable;
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        return null;
    }

    public async ValueTask<bool> DrainAsync(CancellationToken cancellationToken)
    {
        var states = actorSessions.SnapshotStates();
        if (states.Length == 0) return true;

        var targetsByActorType =
            new Dictionary<string, ZLinkActorDrainTarget[]>(StringComparer.Ordinal);
        foreach (var actorType in states
                     .Select(static state => state.ActorType)
                     .Where(static actorType => !string.IsNullOrWhiteSpace(actorType))
                     .Distinct(StringComparer.Ordinal))
        {
            targetsByActorType[actorType!] = await ResolveTargetsAsync(actorType!, cancellationToken)
                .ConfigureAwait(false);
        }

        var nextTarget = -1;
        var moves = states.Select(state => MoveActorAsync(state).AsTask()).ToArray();
        var results = await Task.WhenAll(moves).ConfigureAwait(false);
        return results.All(static moved => moved);

        async ValueTask<bool> MoveActorAsync(ZLinkActorRuntimeState actorState)
        {
            if (actorState.Handoff.IsSourceMigrationInProgress)
            {
                await actorState.Handoff.WaitForSourceCompletionAsync(cancellationToken)
                    .ConfigureAwait(false);
                if (actorState.Actor is null)
                {
                    ZLinkRuntimeMetrics.RecordDrainActorHandedOff();
                    return true;
                }
            }

            var actor = actorState.Actor;
            var sourceNode = actorState.NativeActorRef?.NodeRid;
            var actorType = actorState.ActorType;
            if (actor is null || sourceNode is null || string.IsNullOrWhiteSpace(actorType)) return true;
            if (!targetsByActorType.TryGetValue(actorType, out var targets)) return false;
            var eligible = targets.Where(target => target.NodeRid != sourceNode.Value).ToArray();
            if (eligible.Length == 0) return false;

            var start = (Interlocked.Increment(ref nextTarget) & int.MaxValue) % eligible.Length;
            for (var attempt = 0; attempt < eligible.Length; attempt++)
            {
                var target = eligible[(start + attempt) % eligible.Length];
                try
                {
                    // Entry Spot itself is not relocated. Its stable Spot ID
                    // selects the target admission transaction that restores
                    // this Actor and preserves ObjectGeneration.
                    var result = await actors.JoinActorAsync(
                            target.EntrySpotId,
                            actor,
                            ZLinkMessage.Empty,
                            operationId: null,
                            cancellationToken)
                        .ConfigureAwait(false);
                    if (result is not ZLinkActorJoinResult.Accepted)
                    {
                        ZLinkFrameworkDebugLog.SpotDiscovery(
                            $"drain handoff rejected actor={actorState.ActorId} target={target.NodeRid} result=rejected");
                        continue;
                    }
                    ZLinkRuntimeMetrics.RecordDrainActorHandedOff();
                    return true;
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    throw;
                }
                catch (ZLinkFrameworkException error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff rejected actor={actorState.ActorId} target={target.NodeRid} kind={error.Kind} message={error.Message}");
                    // A peer can leave or reject admission after the location
                    // snapshot. Try the remaining compatible entries before
                    // the next bounded drain pass refreshes the store view.
                }
                catch (ZlinkSubmitException error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff submit deferred actor={actorState.ActorId} target={target.NodeRid} message={error.Message}");
                    // A native route request can be temporarily busy. The
                    // next bounded drain pass retries with a refreshed view.
                }
                catch (TimeoutException error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff timed out actor={actorState.ActorId} target={target.NodeRid} message={error.Message}");
                    // Target availability can change during one request. The
                    // global drain deadline, not one request timeout, owns the
                    // terminal DeadlineExceeded decision.
                }
                catch (ZLinkActorHandoffRejectedException error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff rejected actor={actorState.ActorId} target={target.NodeRid} message={error.Message}");
                    // A completed rollback leaves the source actor eligible
                    // for the next bounded target refresh.
                }
            }
            return false;
        }
    }

    internal static string? ResolveMeshName(
        ZLinkFrameworkRegistration registration,
        string actorType)
    {
        var actorNode = registration.SpotNodes.Values.SingleOrDefault(
            node => node.ActorFactories.ContainsKey(actorType));
        return actorNode is null
            ? null
            : actorNode.SpotMeshChannelName ?? actorNode.SpotNodeName;
    }

    private async ValueTask<ZLinkActorDrainTarget[]> ResolveTargetsAsync(
        string actorType,
        CancellationToken cancellationToken)
    {
        if (services.GetService<IZLinkMeshNodeLocationResolver>() is not { } peers)
            return [];

        var meshName = ResolveMeshName(registration, actorType);
        if (meshName is null) return [];
        // Descriptors carry no actor-type capability set: a non-draining
        // mesh member is a candidate and the target's join admission
        // rejects actor types it has no factory for.
        var descriptors = await peers.ListLiveMeshNodesAsync(meshName, cancellationToken)
            .ConfigureAwait(false);
        var targets = new Dictionary<string, ZLinkActorDrainTarget>(StringComparer.Ordinal);
        foreach (var descriptor in descriptors)
            if (descriptor.State != ZLinkFrameworkRuntimeState.Draining
                && descriptor.Rid is { Size: > 0 }
                && !string.IsNullOrWhiteSpace(descriptor.EntrySpotId))
                targets[descriptor.Rid.ToHex()] = new ZLinkActorDrainTarget(
                    descriptor.Rid,
                    descriptor.EntrySpotId);
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"drain targets actorType={actorType} mesh={meshName} peers={descriptors.Count} accepting={targets.Count}");
        return targets.Values.ToArray();
    }
}

internal readonly record struct ZLinkActorDrainTarget(
    RoutingId NodeRid,
    string EntrySpotId);
