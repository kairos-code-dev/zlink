namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    private readonly HashSet<string> _actorsLeavingForEntrySpot = new(StringComparer.Ordinal);

    public ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actor.ActorId,
                ct => LeaveActorCoreAsync(actor, ct)))
            return ValueTask.CompletedTask;

        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? LeaveActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.LeaveActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    public bool TryResolveActorJoinDescriptor(out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _actorJoins.TryResolve(out descriptor);
    }

    public bool TryResolveActorPacketDescriptor(
        Type actorType,
        ZlinkStreamHeader header,
        out ZLinkSpotActorPacketDescriptor? descriptor)
    {
        descriptor = null;
        return _actorHandlers is not null
               && _actorHandlers.TryResolve(actorType, header, out descriptor);
    }

    public bool TryGetJoinedActor(
        string actorId,
        out IZLinkActor? actor)
    {
        return _actors.TryGetActor(actorId, out actor);
    }

    public async ValueTask<ZLinkSpotActorJoinResult> JoinActorAsync(
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(out var descriptor)
            || descriptor is null)
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not declare an actor join callback.");

        TraceActorJoin(ZLinkMessageFlowOutcome.Received, actor.ActorId);
        var state = new ActorJoinCallState(actor, request, descriptor);
        if (ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this))
        {
            state.Result = await InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    cancellationToken)
                .ConfigureAwait(false);
            if (state.Result.Accepted)
                await CommitActorJoinCoreAsync(state.Actor, cancellationToken)
                    .ConfigureAwait(false);

            TraceActorJoin(ZLinkMessageFlowOutcome.Replied, actor.ActorId);
            return state.Result;
        }

        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Result = await activation.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    ct);
                if (state.Result.Accepted)
                    await activation.CommitActorJoinCoreAsync(state.Actor, ct)
                        .ConfigureAwait(false);
            },
            state,
            cancellationToken);

        TraceActorJoin(ZLinkMessageFlowOutcome.Replied, actor.ActorId);
        return state.Result;
    }

    private void TraceActorJoin(ZLinkMessageFlowOutcome outcome, string actorId)
    {
        if (!_runtime.Flow.Enabled(outcome)) return;

        _runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            outcome,
            ZLinkDispatchErrorSurface.SpotActor,
            ZLinkDispatchMessageKind.ActorRequest,
            "JoinSpot",
            ChannelName,
            ActorId: actorId,
            SpotRid: SpotRid.ToString()));
    }

    public async ValueTask<ZLinkSpotActorJoinResult> AdmitRemoteActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(out var descriptor)
            || descriptor is null)
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not declare an actor join callback.");

        var state = new ActorJoinAdmissionCallState(actorId, request, descriptor);
        if (ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this))
        {
            state.Result = await HandlerInvoker.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.ActorId,
                    state.Request,
                    cancellationToken)
                .ConfigureAwait(false);
            return state.Result;
        }

        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Result = await activation.HandlerInvoker.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.ActorId,
                    state.Request,
                    ct);
            },
            state,
            cancellationToken);

        return state.Result;
    }

    internal async ValueTask<ZLinkSpotActorJoinResult> AdmitActorJoinFromCallerTurnAsync(
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        ArgumentNullException.ThrowIfNull(request);
        if (!_actorJoins.TryResolve(out var descriptor) || descriptor is null)
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not declare an actor join callback.");

        TraceActorJoin(ZLinkMessageFlowOutcome.Received, actor.ActorId);
        var result = await InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
        TraceActorJoin(ZLinkMessageFlowOutcome.Replied, actor.ActorId);
        return result;
    }

    internal ValueTask CommitActorJoinFromCallerTurnAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return CommitActorJoinCoreAsync(actor, cancellationToken);
    }

    public ValueTask PrepareTransferredActorJoinAndReplayAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? PrepareTransferredActorJoinAndReplayCoreAsync(actor, actorState, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.PrepareTransferredActorJoinAndReplayCoreAsync(
                    state.Actor,
                    state.ActorState,
                    ct),
                (Actor: actor, ActorState: actorState),
                cancellationToken);
    }

    private async ValueTask PrepareTransferredActorJoinAndReplayCoreAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        try
        {
            // Materialize membership during prepare, but do not expose the join to application
            // code until the distributed handoff has committed. A joined callback may push a
            // client notification, and that notification must not overtake source cutover.
            await JoinActorToSpotCoreAsync(
                    actor,
                    notifyJoined: false,
                    cancellationToken: cancellationToken)
                .ConfigureAwait(false);

        }
        catch
        {
            _actors.RemoveIfCurrent(actor);
            actorState.LeaveSpotIfCurrent(this);
            throw;
        }
    }

    internal ValueTask CompleteTransferredActorJoinAsync(
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        var actor = actorState.Actor
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actorState.ActorId}' has no transferred instance at commit.");
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyJoinedActorCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.NotifyJoinedActorCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    internal async ValueTask ReplayTransferredActorHandoffAsync(
        ZLinkActorRuntimeState actorState,
        IReadOnlyList<ZLinkActorHandoffFrame> sourceFrames,
        CancellationToken cancellationToken)
    {
        var frames = actorState.Handoff.PrepareImportedReplay(sourceFrames);
        if (frames.Count == 0) return;

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during handoff completion.");
        foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"backlog_enqueued actor={actorState.ActorId} arrival={frame.ArrivalIndex} trailing=true request_id={frame.RequestId} flags={frame.Flags}");
        await _dispatcher.DispatchActorReplayFramesAsync(
                ZLinkActorHandoffFrames.Restore(actorRef, frames),
                actorState.Handoff.AcknowledgeReplayedFrame,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask ReplayFinalTransferredActorHandoffAsync(
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during final handoff replay.");
        while (true)
        {
            var frames = actorState.Handoff.SnapshotFinalReplay();
            if (frames.Count == 0) return;

            await _dispatcher.DispatchActorReplayFramesAsync(
                    ZLinkActorHandoffFrames.Restore(actorRef, frames),
                    actorState.Handoff.AcknowledgeReplayedFrame,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    internal ValueTask ReplayAbortedActorHandoffAsync(
        ZLinkActorRuntimeState actorState,
        IReadOnlyList<ZLinkActorHandoffFrame> frames,
        CancellationToken cancellationToken)
    {
        if (frames.Count == 0) return ValueTask.CompletedTask;

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during handoff rollback.");
        return _dispatcher.DispatchActorFramesAsync(
            ZLinkActorHandoffFrames.Restore(actorRef, frames),
            cancellationToken);
    }

    internal ValueTask RestoreActorAfterFailedHandoffAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? RestoreActorAfterFailedHandoffCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) => activation.RestoreActorAfterFailedHandoffCoreAsync(state, ct),
                actor,
                cancellationToken);
    }

    private async ValueTask RestoreActorAfterFailedHandoffCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await JoinActorToSpotCoreAsync(
                actor,
                notifyJoined: true,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
        if (_runtime.LocationLifecycle is { } locations)
        {
            _ = locations.SpotLocations.TryGetTrackedGeneration(SpotRid, out var spotGeneration);
            await locations.ActorOwnership.NotifyActorJoinedSpotAsync(
                    actor.ActorId,
                    SpotRid,
                    spotGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public ValueTask NotifyActorDisconnectedAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyActorDisconnectedCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
            static (activation, state, ct) =>
                activation.NotifyActorDisconnectedCoreAsync(state, ct),
            actor,
            cancellationToken);
    }

    internal ValueTask NotifyActorLeftAfterNativeJoinEntrySpotAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyActorLeftAfterNativeJoinEntrySpotCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) =>
                    activation.NotifyActorLeftAfterNativeJoinEntrySpotCoreAsync(
                        state,
                        ct),
                actor,
                cancellationToken);
    }

    internal ValueTask NotifyActorLeftAfterManagedJoinSpotAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyActorLeftAfterJoinCommitCoreAsync(actor, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) =>
                    activation.NotifyActorLeftAfterJoinCommitCoreAsync(
                        state,
                        ct),
                actor,
                cancellationToken);
    }

    private async ValueTask CommitActorJoinCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await JoinActorToSpotCoreAsync(
                actor,
                notifyJoined: true,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        if (_runtime.LocationLifecycle is { } locations)
        {
            var actorState = _runtime.GetOrCreateActorState(actor.ActorId);
            _ = locations.SpotLocations.TryGetTrackedGeneration(SpotRid, out var spotGeneration);
            await locations.ActorOwnership.NotifyActorJoinedSpotAsync(
                    actor.ActorId,
                    SpotRid,
                    spotGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"location_committed actor={actor.ActorId} spot={SpotRid}");
        }
    }

    private async ValueTask JoinActorToSpotCoreAsync(
        IZLinkActor actor,
        bool notifyJoined,
        CancellationToken cancellationToken)
    {
        _actorsLeavingForEntrySpot.Remove(actor.ActorId);
        var previousActivation = _runtime.GetOrCreateActorState(actor.ActorId).Activation;
        _actors.Add(actor);
        await _runtime.JoinActorToSpotAsync(this, actor, cancellationToken).ConfigureAwait(false);
        if (ReferenceEquals(previousActivation, this)) return;

        if (previousActivation is null)
            await _runtime.NotifyEntrySpotActorLeftAsync(actor, NodeRid, cancellationToken)
                .ConfigureAwait(false);

        if (notifyJoined)
            await NotifyJoinedActorCoreAsync(actor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask NotifyJoinedActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        if (_actorHandlers is not null
            && _actorHandlers.TryResolveJoined(actor.GetType(), out var descriptor)
            && descriptor is not null)
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
    }

    private async ValueTask LeaveActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        _actorsLeavingForEntrySpot.Add(actor.ActorId);
        try
        {
            await _runtime.JoinActorEntrySpotAsync(NodeRid, actor, ZLinkMessage.Empty, cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            _actorsLeavingForEntrySpot.Remove(actor.ActorId);
            throw;
        }
    }

    private async ValueTask NotifyActorLeftAfterNativeJoinEntrySpotCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await NotifyActorLeftAfterJoinCommitCoreAsync(actor, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask NotifyActorLeftAfterJoinCommitCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        _actors.RemoveIfCurrent(actor);
        var actorState = _runtime.GetOrCreateActorState(actor.ActorId);
        actorState.LeaveSpotIfCurrent(this);

        if (_runtime.LocationLifecycle is { } locations)
            await locations.ActorOwnership.NotifyActorLeftSpotAsync(
                    actor.ActorId,
                    cancellationToken)
                .ConfigureAwait(false);

        if (_actorHandlers is not null
            && _actorHandlers.TryResolveLeft(actor.GetType(), out var descriptor)
            && descriptor is not null)
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
    }

    private async ValueTask NotifyActorDisconnectedCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        if (_actorsLeavingForEntrySpot.Remove(actor.ActorId)) return;

        if (_actorHandlers is not null
            && _actorHandlers.TryResolveDisconnected(actor.GetType(), out var descriptor)
            && descriptor is not null)
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkSpotActorJoinResult> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor.ActorId, request, cancellationToken)
            .ConfigureAwait(false);
    }

    private sealed class ActorJoinCallState(
        IZLinkActor actor,
        ZLinkMessage request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public IZLinkActor Actor { get; } = actor;

        public ZLinkMessage Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public ZLinkSpotActorJoinResult Result { get; set; }
    }

    private sealed class ActorJoinAdmissionCallState(
        string actorId,
        ZLinkMessage request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public string ActorId { get; } = actorId;

        public ZLinkMessage Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public ZLinkSpotActorJoinResult Result { get; set; }
    }
}
