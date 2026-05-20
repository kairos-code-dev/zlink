namespace Zlink.Framework.Runtime.Core;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        return await _actors.JoinActorAsync<TRequest, TReply>(
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    internal async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.JoinActorToSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
        => await _actors.LeaveActorFromSpotAsync(activation, actor, cancellationToken);

    internal async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.AttachActorAsync(actor, stream, cancellationToken);

    internal async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
        => await _actors.DisconnectActorAsync(actor, stream, cancellationToken);

    internal async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorAsync(actor, header, payload, cancellationToken);

    internal async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
        => await _actors.CreateLocalActorAsync(actorId, actorType, cancellationToken);

    internal async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
        => await _actors.CreateActorAsync(actorId, actorType, cancellationToken);

    internal async ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
        => await _actors.FindActorAsync(actorId, cancellationToken);

    internal bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
        => _actors.TryGetCreatedActor(actorId, actorType, out actor);

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
        => _actors.TryGetCreatedActorState(actorId, actorType, out state);

    internal async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken);

    internal async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
        => await _actors.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);

    internal async ValueTask NotifyActorDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
        => await _actors.NotifyDisconnectedByIdAsync(actorId, cancellationToken);

    internal ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
        => _actors.GetOrCreateActorState(actorId);

    internal string ResolveDefaultRouterChannelId()
    {
        if (_registration.RouteChannels.Count != 1)
        {
            throw new InvalidOperationException("Exactly one routed channel is required for session actor dispatch.");
        }

        return _registration.RouteChannels.Keys.Single();
    }

    internal ZLinkActorRoute ResolveLocalActorRoute(
        string actorId,
        string actorType)
    {
        if (!TryGetCreatedActorState(actorId, actorType, out var state))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' is not created on the local actor runtime.");
        }

        var routerChannelId = ResolveDefaultRouterChannelId();
        return new ZLinkActorRoute(
            routerChannelId,
            ResolveSessionRouterId(routerChannelId),
            state.CurrentActorGeneration);
    }

    internal RoutingId ResolveSessionRouterId(string routerChannelId)
    {
        if (!_registration.RouteChannels.TryGetValue(routerChannelId, out var routed)
            || routed.RoutingConfig.RoutingId.Size == 0)
        {
            throw new InvalidOperationException($"Route channel '{routerChannelId}' must configure a routing id.");
        }

        return routed.RoutingConfig.RoutingId;
    }

    internal void BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkActorRef actorRef)
    {
        _sessionBindings.Bind(actorId, context, bindingToken, actorRef);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionBindings.Unbind(actorId, context, bindingToken);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _sessionBindings.TryGet(actorId, bindingToken, out context);
    }

    internal ValueTask<int> UpdateAttachedActorRouteAsync(
        string actorId,
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var updated = _sessionBindings.UpdateAttachedActorRoute(
            actorId,
            routerChannelId,
            targetNodeRid,
            expectedActorGeneration,
            newActorGeneration);
        return ValueTask.FromResult(updated);
    }
}
