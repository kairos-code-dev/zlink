
namespace Zlink.Framework.Runtime.Spots;

internal sealed partial class ZLinkSpotActivation
{
    public ValueTask LeaveActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(actor);
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

    public async ValueTask<ZLinkSpotActorJoinResult> JoinActorAsync(
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(out var descriptor)
            || descriptor is null)
        {
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not declare an actor join callback.");
        }

        var state = new ActorJoinCallState(actor, request, descriptor);
        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Result = await activation.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    ct);
                if (state.Result.Accepted)
                {
                    await activation.CommitActorJoinCoreAsync(state.Actor, ct)
                        .ConfigureAwait(false);
                }
            },
            state,
            cancellationToken);

        return state.Result;
    }

    public ValueTask NotifyActorDisconnectedAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
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
        await _actorLifecycle.JoinAsync(actor, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask LeaveActorCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        await _runtime.JoinActorEntrySpotAsync(NodeRid, actor, cancellationToken)
            .ConfigureAwait(false);
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
        if (ReferenceEquals(actorState.Activation, this))
        {
            actorState.Activation = null;
        }

        if (_actorHandlers is not null
            && _actorHandlers.TryResolveLeft(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask NotifyActorDisconnectedCoreAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        if (_actorHandlers is not null
            && _actorHandlers.TryResolveDisconnected(actor.GetType(), out var descriptor)
            && descriptor is not null)
        {
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask<ZLinkSpotActorJoinResult> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }

    private sealed class ActorJoinCallState(
        IZLinkActor actor,
        Message request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public IZLinkActor Actor { get; } = actor;

        public Message Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public ZLinkSpotActorJoinResult Result { get; set; }
    }
}
