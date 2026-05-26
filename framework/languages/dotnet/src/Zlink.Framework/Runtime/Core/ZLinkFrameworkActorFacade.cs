using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Spots;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    public async ValueTask<ZLinkActorJoinResult<TReply>> JoinActorAsync<TRequest, TReply>(
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();

        if (node is not null
            && actorState.NativeActorRef is { } actorRef
            && actorState.Stream is ZLinkManagedStream
            && actorState.CurrentDispatch is null)
        {
            return await NativeJoinActorAsync<TRequest, TReply>(
                state,
                spotRid,
                actor,
                actorRef,
                node,
                request,
                cancellationToken).ConfigureAwait(false);
        }

        var reply = await spots.JoinActorAsync<TRequest, TReply>(
            state,
            spotRid,
            actor,
            request,
            cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            ResultCode: 0,
            ToActorRef(actorState),
            reply);
    }

    public async ValueTask<ActorRef> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode()
            ?? throw new InvalidOperationException("Entry SPOT join requires a router-capable SpotNode.");
        var actorRef = actorState.NativeActorRef
            ?? throw new InvalidOperationException($"Actor '{actor.ActorId}' does not have a native Actor ref.");
        var previousActivation = actorState.LiveActivation;

        var tcs = new TaskCompletionSource<ZLinkBackendActorJoinEntrySpotResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<ZLinkBackendActorJoinEntrySpotResult>)s!).TrySetCanceled(),
            tcs);

        if (!node.JoinActorEntrySpot(
                actorRef,
                spotNodeRid,
                result => tcs.TrySetResult(result),
                registration.DefaultTimeout))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join submit failed for '{actor.ActorId}'.");
        }

        var result = await tcs.Task.ConfigureAwait(false);
        if (result.Result == RequestResult.NotConnected)
        {
            return await JoinRemoteActorEntrySpotAsync(
                    getState(),
                    spotNodeRid,
                    actor,
                    actorState,
                    actorRef,
                    previousActivation,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (result.Result != RequestResult.Ok)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' with '{result.Result}'.");
        }

        actorState.NativeActorRef = result.Actor;
        await NotifyManagedEntrySpotJoinLifecycleAsync(
                actor,
                previousActivation,
                result.Actor.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
        if (result.Actor.NodeRid != actorRef.NodeRid)
        {
            actorState.InvalidateContext();
        }

        return ToActorRef(result.Actor);
    }

    private async ValueTask<ActorRef> JoinRemoteActorEntrySpotAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkSpotActivation? previousActivation,
        CancellationToken cancellationToken)
    {
        if (TryFindSpotNode(state, spotNodeRid, out var targetNode))
        {
            var localTargetRef = targetNode.Node.ActorLookup(actor.ActorId)
                ?? targetNode.Node.CreateActor(actor.ActorId);
            actorState.NativeActorRef = localTargetRef;
            await NotifyManagedEntrySpotJoinLifecycleAsync(
                    actor,
                    previousActivation,
                    localTargetRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
            if (localTargetRef.NodeRid != sourceActorRef.NodeRid)
            {
                actorState.InvalidateContext();
            }

            return ToActorRef(localTargetRef);
        }

        var routeChannel = state.RouteChannels.Values.FirstOrDefault()
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' because target node '{spotNodeRid}' is not connected and no route channel is registered.");

        var request = new ZLinkActorEntrySpotRouteJoinRequest(
            actor.ActorId,
            actorState.ActorType ?? actor.GetType().Name,
            sourceActorRef.NodeRid.ToHex(),
            previousActivation?.SpotRid.ToHex() ?? string.Empty,
            sourceActorRef.Generation);

        var reply = await routeChannel.RequestAsync<ZLinkActorEntrySpotRouteJoinRequest, ZLinkActorEntrySpotRouteJoinReply>(
                spotNodeRid,
                ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName,
                request,
                registration.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);

        var targetRef = new ZLinkBackendActorRef(
            RoutingId.FromString(reply.TargetNodeRid),
            actor.ActorId,
            reply.ActorGeneration);
        actorState.NativeActorRef = targetRef;
        await NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
                actor,
                previousActivation,
                cancellationToken)
            .ConfigureAwait(false);
        if (targetRef.NodeRid != sourceActorRef.NodeRid)
        {
            actorState.InvalidateContext();
        }

        return ToActorRef(targetRef);
    }

    private static bool TryFindSpotNode(
        ZLinkFrameworkRuntimeState state,
        RoutingId nodeRid,
        out ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var candidate in state.SpotNodes.Values)
        {
            if (candidate.Node.RoutingId == nodeRid)
            {
                nodeRuntime = candidate;
                return true;
            }
        }

        nodeRuntime = null!;
        return false;
    }

    private async ValueTask NotifyManagedEntrySpotJoinLifecycleAsync(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken)
    {
        if (previousActivation is null)
        {
            return;
        }

        await NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
                actor,
                previousActivation,
                cancellationToken)
            .ConfigureAwait(false);

        await spots.NotifyEntrySpotActorJoinedAsync(
                getState(),
                actor,
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JoinEntrySpot),
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        CancellationToken cancellationToken)
    {
        if (previousActivation is null)
        {
            return;
        }

        await previousActivation.NotifyActorLeftAfterNativeJoinEntrySpotAsync(
                actor,
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JoinEntrySpot),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.AttachActorAsync(actor, stream, cancellationToken);
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorAsync(actor, header, payload, cancellationToken);
    }

    public async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.CreateAndBindActorAsync(actorId, actorType, cancellationToken);
    }

    public async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.CreateActorAsync(actorId, actorType, cancellationToken);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
    {
        return actorSessionManager.TryGetCreatedActorState(actorId, actorType, out state);
    }

    public async ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.FindActorAsync(actorId, cancellationToken);
    }

    public bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
    {
        return actorSessionManager.TryGetCreatedActor(actorId, actorType, out actor);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken);
    }

    public async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);
    }

    public async ValueTask NotifyDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.NotifyDisconnectedByIdAsync(actorId, cancellationToken);
    }

    public ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
    {
        return actorSessionManager.GetOrCreateState(actorId);
    }

    private async ValueTask<ZLinkActorJoinResult<TReply>> NativeJoinActorAsync<TRequest, TReply>(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        TRequest request,
        CancellationToken cancellationToken)
    {
        var activation = spots.GetActivationBySpotRid(state, spotRid)
            ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        if (!activation.TryResolveActorJoinDescriptor(typeof(TRequest), out var descriptor) || descriptor is null)
        {
            throw new InvalidOperationException(
                $"SPOT '{activation.SpotRid}' does not register an actor join handler for '{typeof(TRequest)}'.");
        }

        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            activation.ChannelName,
            descriptor.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        var joinParts = ZLinkEnvelopeCodec.EncodeParts(joinHeader, request, typeof(TRequest));

        var tcs = new TaskCompletionSource<(ZLinkBackendActorJoinResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<(ZLinkBackendActorJoinResult, IReadOnlyList<Message>)>)s!).TrySetCanceled(),
            tcs);

        var submitted = node.JoinActor(
            actorRef,
            activation.NodeRid,
            activation.SpotRid,
            joinParts,
            (result, reply) => tcs.TrySetResult((result, reply)),
            registration.DefaultTimeout);
        ZLinkMessageParts.DisposeAll(joinParts);

        if (!submitted)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor join submit failed for '{actor.ActorId}' to SPOT '{activation.SpotRid}'.");
        }

        var (joinResult, replyParts) = await tcs.Task.ConfigureAwait(false);
        var reply = DecodeNativeJoinReply<TRequest, TReply>(
            joinResult.Result,
            replyParts,
            actor.ActorId,
            activation.SpotRid);
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        actorState.NativeActorRef = joinResult.Actor;
        if (joinResult.Actor.NodeRid != actorRef.NodeRid)
        {
            actorState.InvalidateContext();
        }

        return new ZLinkActorJoinResult<TReply>(
            ResultCode: joinResult.JoinResultCode,
            ToActorRef(joinResult.Actor),
            reply);
    }

    private static ActorRef ToActorRef(ZLinkActorRuntimeState actorState)
    {
        var actorRef = actorState.NativeActorRef
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorState.ActorId}' does not have a native Actor ref.");
        return ToActorRef(actorRef);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    private static TReply DecodeNativeJoinReply<TRequest, TReply>(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotRid)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor join was rejected for '{actorId}' to SPOT '{spotRid}'.");
            }

            if (replyParts.Count == 0)
            {
                throw new InvalidOperationException(
                    $"Actor join reply for '{typeof(TRequest)}' was empty.");
            }

            var replyObj = ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(TReply));
            return (TReply?)replyObj
                ?? throw new InvalidOperationException($"Actor join reply for '{typeof(TRequest)}' was null.");
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
