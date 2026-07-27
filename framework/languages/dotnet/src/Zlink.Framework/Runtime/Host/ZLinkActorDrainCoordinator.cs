using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Host;

/// <summary>
/// Coordinates actor target discovery and managed handoff during runtime drain.
/// </summary>
internal sealed class ZLinkActorDrainCoordinator(
    ZLinkStandaloneActorRelocationRuntime relocation,
    ZLinkActorSessionManager actorSessions,
    IServiceProvider services,
    ZLinkFrameworkRegistration registration)
{
    public async ValueTask<ZLinkFrameworkRelocationReason?> PreflightAsync(
        ZLinkRetirePreflightPlan plan,
        ZLinkRelocationTargetSelection selection,
        CancellationToken cancellationToken)
    {
        var states = StandaloneActors(actorSessions.SnapshotStates());
        if (states.Length == 0)
            return null;

        try
        {
            foreach (var actorType in states
                         .Select(static state => state.ActorType)
                         .Where(static actorType => !string.IsNullOrWhiteSpace(actorType))
                         .Distinct(StringComparer.Ordinal))
            {
                var sourceNode = registration.SpotNodes.Values.Single(node =>
                    node.ActorFactories.ContainsKey(actorType!));
                if (sourceNode.ActorRelocations[actorType!].PolicyKind == 0)
                    return ZLinkFrameworkRelocationReason.RelocationDisabled;
                var targets = await ResolveTargetCandidatesAsync(
                        actorType!,
                        selection,
                        cancellationToken)
                    .ConfigureAwait(false);
                foreach (var state in states.Where(state =>
                             string.Equals(state.ActorType, actorType, StringComparison.Ordinal)))
                {
                    if (state.Actor is null || state.NativeActorRef is not { } actorRef)
                        continue;
                    var capacity = new ZLinkCapacityVector(1, 0, null);
                    if (!targets.Any(target =>
                            target.Target.NodeRid != actorRef.NodeRid
                            && plan.TryReserve(target.Descriptor, capacity)))
                        return ZLinkFrameworkRelocationReason.TargetUnavailable;
                }
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        return null;
    }

    public async ValueTask<ZLinkActorDrainResult> DrainAsync(
        ZLinkRelocationTargetSelection selection,
        CancellationToken cancellationToken)
    {
        var states = StandaloneActors(actorSessions.SnapshotStates());
        if (states.Length == 0) return new ZLinkActorDrainResult(true, null, 0);

        var targetsByActorType =
            new Dictionary<string, ZLinkActorDrainCandidate[]>(StringComparer.Ordinal);
        foreach (var actorType in states
                     .Select(static state => state.ActorType)
                     .Where(static actorType => !string.IsNullOrWhiteSpace(actorType))
                     .Distinct(StringComparer.Ordinal))
        {
            targetsByActorType[actorType!] = (await ResolveTargetCandidatesAsync(
                    actorType!,
                    selection,
                    cancellationToken)
                .ConfigureAwait(false));
        }

        var nextTarget = -1;
        var moves = states.Select(state => MoveActorAsync(state).AsTask()).ToArray();
        var results = await Task.WhenAll(moves).ConfigureAwait(false);
        var terminal = results.FirstOrDefault(
            static result => result.TerminalReason is not null);
        return terminal.TerminalReason is not null
            ? terminal
            : new ZLinkActorDrainResult(
                results.All(static result => result.Completed),
                null,
                checked((ulong)results.Sum(static result =>
                    checked((long)result.CommittedUnitCount))));

        async ValueTask<ZLinkActorDrainResult> MoveActorAsync(
            ZLinkActorRuntimeState actorState)
        {
            if (actorState.Handoff.IsSourceMigrationInProgress)
            {
                await actorState.Handoff.WaitForSourceCompletionAsync(cancellationToken)
                    .ConfigureAwait(false);
                if (actorState.Actor is null)
                {
                    ZLinkRuntimeMetrics.RecordDrainActorHandedOff();
                    return new ZLinkActorDrainResult(true, null, 1);
                }
            }

            var actor = actorState.Actor;
            var sourceNode = actorState.NativeActorRef?.NodeRid;
            var actorType = actorState.ActorType;
            if (actor is null || sourceNode is null || string.IsNullOrWhiteSpace(actorType))
                return new ZLinkActorDrainResult(true, null, 0);
            if (!targetsByActorType.TryGetValue(actorType, out var targets))
                return new ZLinkActorDrainResult(false, null, 0);
            var eligible = targets.Where(target =>
                target.Target.NodeRid != sourceNode.Value).ToArray();
            if (eligible.Length == 0)
                return new ZLinkActorDrainResult(false, null, 0);

            var start = (Interlocked.Increment(ref nextTarget) & int.MaxValue) % eligible.Length;
            for (var attempt = 0; attempt < eligible.Length; attempt++)
            {
                var candidate = eligible[(start + attempt) % eligible.Length];
                var target = candidate.Target;
                try
                {
                    var completed = await relocation.RelocateSourceAsync(
                            actorState,
                            candidate.Descriptor,
                            cancellationToken)
                        .ConfigureAwait(false);
                    if (!completed)
                    {
                        ZLinkFrameworkDebugLog.SpotDiscovery(
                            $"drain handoff rejected actor={actorState.ActorId} target={target.NodeRid} result=rejected");
                        continue;
                    }
                    ZLinkRuntimeMetrics.RecordDrainActorHandedOff();
                    return new ZLinkActorDrainResult(true, null, 1);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    throw;
                }
                catch (ZLinkFrameworkException error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff rejected actor={actorState.ActorId} target={target.NodeRid} kind={error.Kind} message={error.Message}");
                    if (!IsTargetLocalRetriable(error))
                        return new ZLinkActorDrainResult(
                            false,
                            ZLinkFrameworkRelocationReason.RelocationFailed,
                            0);
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
                catch (Exception error)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"drain handoff terminal actor={actorState.ActorId} target={target.NodeRid} message={error.Message}");
                    return new ZLinkActorDrainResult(
                        false,
                        ZLinkFrameworkRelocationReason.RelocationFailed,
                        0);
                }
            }
            return new ZLinkActorDrainResult(false, null, 0);
        }
    }

    internal static bool IsTargetLocalRetriable(ZLinkFrameworkException error) =>
        error.IsRetriable
        && error.Kind is ZLinkFrameworkErrorKind.RouteNotConnected
            or ZLinkFrameworkErrorKind.ActorLocationStale
            or ZLinkFrameworkErrorKind.ActorMoving
            or ZLinkFrameworkErrorKind.DeadlineExceeded
            or ZLinkFrameworkErrorKind.PlacementCapacityExhausted
            or ZLinkFrameworkErrorKind.SpotMoving
            or ZLinkFrameworkErrorKind.RelocationTargetUnavailable;

    internal static ZLinkActorRuntimeState[] StandaloneActors(
        IEnumerable<ZLinkActorRuntimeState> states) =>
        states.Where(static state => state.LiveActivation is null).ToArray();

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

    private async ValueTask<ZLinkActorDrainCandidate[]> ResolveTargetCandidatesAsync(
        string actorType,
        ZLinkRelocationTargetSelection selection,
        CancellationToken cancellationToken)
    {
        if (services.GetService<IZLinkMeshNodeLocationResolver>() is not { } peers)
            return [];

        var meshName = ResolveMeshName(registration, actorType);
        if (meshName is null) return [];
        var sourceNode = registration.SpotNodes.Values.Single(node =>
            node.ActorFactories.ContainsKey(actorType));
        var sourcePolicy = sourceNode.ActorRelocations[actorType];
        var requiredPolicy = sourcePolicy.PolicyKind switch
        {
            0 => ZLinkObjectMaintenancePolicyKind.Disabled,
            1 => ZLinkObjectMaintenancePolicyKind.Recreate,
            2 => ZLinkObjectMaintenancePolicyKind.Snapshot,
            _ => throw new ZLinkConfigurationException(
                $"Unknown relocation policy kind '{sourcePolicy.PolicyKind}'.")
        };
        if (requiredPolicy == ZLinkObjectMaintenancePolicyKind.Disabled)
            return [];
        var descriptors = await peers.ListLiveMeshNodesAsync(meshName, cancellationToken)
            .ConfigureAwait(false);
        var localNodeRids = registration.SpotNodes.Values
            .Select(static node => node.RoutingId)
            .ToHashSet();
        var targets = new Dictionary<string, ZLinkActorDrainCandidate>(StringComparer.Ordinal);
        foreach (var descriptor in descriptors)
            if (!localNodeRids.Contains(descriptor.Rid)
                && descriptor.State == ZLinkFrameworkRuntimeState.Serving
                && descriptor.ObjectRole == ZLinkMeshNodeObjectRole.Server
                && descriptor.PlacementWeight > 0
                && descriptor.LeaseGeneration > 0
                && descriptor.Rid is { Size: > 0 }
                && !string.IsNullOrWhiteSpace(descriptor.EntrySpotId)
                && selection.Matches(descriptor)
                && (registration.MaintenanceWave is null
                    || !StringComparer.Ordinal.Equals(
                        registration.MaintenanceWave,
                        descriptor.MaintenanceWave))
                && ZLinkSpotRetireTargetRuntime.HasHeadroom(
                    descriptor.Capacity.Actors,
                    1)
                && descriptor.ActivationConcurrency.Limit
                   - descriptor.ActivationConcurrency.Active >= 1
                && descriptor.ObjectCapabilities.Any(capability =>
                    capability.ObjectKind == ZLinkPlacementObjectKind.Actor
                    && StringComparer.Ordinal.Equals(
                        capability.StableType,
                        actorType)
                    && capability.Policy == requiredPolicy
                    && (requiredPolicy
                        != ZLinkObjectMaintenancePolicyKind.Snapshot
                        || capability.HasSnapshotAdapter)))
                targets[descriptor.Rid.ToHex()] = new ZLinkActorDrainCandidate(
                    descriptor,
                    new ZLinkActorDrainTarget(
                        descriptor.Rid,
                        descriptor.EntrySpotId));
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"drain targets actorType={actorType} mesh={meshName} peers={descriptors.Count} accepting={targets.Count}");
        return targets.Values.ToArray();
    }
}

internal readonly record struct ZLinkActorDrainTarget(
    RoutingId NodeRid,
    string EntrySpotId);

internal readonly record struct ZLinkActorDrainCandidate(
    ZLinkMeshNodeDescriptor Descriptor,
    ZLinkActorDrainTarget Target);

internal readonly record struct ZLinkActorDrainResult(
    bool Completed,
    ZLinkFrameworkRelocationReason? TerminalReason,
    ulong CommittedUnitCount)
{
    internal bool HasCommitted => CommittedUnitCount != 0;
}
