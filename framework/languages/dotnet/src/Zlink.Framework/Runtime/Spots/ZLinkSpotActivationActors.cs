
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

    public bool TryResolveActorJoinDescriptor(
        Type requestType,
        out ZLinkSpotActorJoinDescriptor? descriptor)
    {
        return _actorJoins.TryResolve(requestType, out descriptor);
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

    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);

        if (!_actorJoins.TryResolve(typeof(TRequest), out var descriptor)
            || descriptor is null)
        {
            throw new InvalidOperationException(
                $"SPOT '{Spot.GetType()}' does not register an actor join handler for '{typeof(TRequest)}'.");
        }

        var state = new ActorJoinCallState(actor, request, descriptor);
        await ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                state.Reply = await activation.InvokeActorJoinAsync(
                    state.Descriptor,
                    state.Actor,
                    state.Request,
                    ct);
                await activation.CommitActorJoinCoreAsync(state.Actor, ct)
                    .ConfigureAwait(false);
            },
            state,
            cancellationToken);

        return (TReply?)state.Reply
            ?? throw new InvalidOperationException(
                $"SPOT actor join reply for '{typeof(TRequest)}' was null.");
    }

    public ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            async static (activation, state, ct) =>
            {
                await activation.LeaveActorCoreAsync(state, ct);
                await state.OnDisconnectedAsync(ct);
            },
            actor,
            cancellationToken);
    }

    public ValueTask NotifyActorDisconnectedAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        return ExecuteSerializedAsync(
            async static (_, state, ct) => await state.OnDisconnectedAsync(ct),
            actor,
            cancellationToken);
    }

    internal ValueTask NotifyActorLeftAfterNativeJoinEntrySpotAsync(
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyActorLeftAfterNativeJoinEntrySpotCoreAsync(actor, context, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) =>
                    activation.NotifyActorLeftAfterNativeJoinEntrySpotCoreAsync(
                        state.Actor,
                        state.Context,
                        ct),
                new ActorLifecycleNotificationState(actor, context),
                cancellationToken);
    }

    internal ValueTask NotifyActorLeftAfterManagedJoinSpotAsync(
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(actor);
        return ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, this)
            ? NotifyActorLeftAfterJoinCommitCoreAsync(actor, context, cancellationToken)
            : ExecuteSerializedAsync(
                static (activation, state, ct) =>
                    activation.NotifyActorLeftAfterJoinCommitCoreAsync(
                        state.Actor,
                        state.Context,
                        ct),
                new ActorLifecycleNotificationState(actor, context),
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
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        await NotifyActorLeftAfterJoinCommitCoreAsync(actor, context, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask NotifyActorLeftAfterJoinCommitCoreAsync(
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
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
            await HandlerInvoker.InvokeActorLifecycleAsync(descriptor, actor, context, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        return await HandlerInvoker.InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }

    private sealed class ActorJoinCallState(
        IZLinkActor actor,
        object request,
        ZLinkSpotActorJoinDescriptor descriptor)
    {
        public IZLinkActor Actor { get; } = actor;

        public object Request { get; } = request;

        public ZLinkSpotActorJoinDescriptor Descriptor { get; } = descriptor;

        public object? Reply { get; set; }
    }

    private sealed record ActorLifecycleNotificationState(
        IZLinkActor Actor,
        ZLinkSpotActorChangeResult Context);
}
