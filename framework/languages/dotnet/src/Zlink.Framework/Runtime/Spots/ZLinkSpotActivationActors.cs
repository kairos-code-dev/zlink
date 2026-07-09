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

        return state.Result;
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
        await JoinActorToSpotCoreAsync(actor, cancellationToken).ConfigureAwait(false);

        if (_runtime.LocationLifecycle is { } locations)
        {
            var actorState = _runtime.GetOrCreateActorState(actor.ActorId);
            await locations.ActorOwnership.NotifyActorJoinedSpotAsync(
                    actorState.ActorType ?? string.Empty,
                    actor.ActorId,
                    SpotRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask JoinActorToSpotCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        var previousActivation = _runtime.GetOrCreateActorState(actor.ActorId).Activation;
        _actors.Add(actor);
        await _runtime.JoinActorToSpotAsync(this, actor, cancellationToken).ConfigureAwait(false);
        if (ReferenceEquals(previousActivation, this)) return;

        if (previousActivation is null)
            await _runtime.NotifyEntrySpotActorLeftAsync(actor, NodeRid, cancellationToken)
                .ConfigureAwait(false);

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
        _actorsLeavingForEntrySpot.Remove(actor.ActorId);
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
                    actorState.ActorType ?? string.Empty,
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
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
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
}
