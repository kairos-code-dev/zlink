using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Spots;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorEntrySpotJoinCoordinator _entrySpotJoin = new(
        registration,
        spots,
        actorSessionManager,
        getState,
        getActorSpotNode);

    public async ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();

        if (actorState.Stream is ZLinkManagedStream
            && actorState.CurrentDispatch is null
            && node is not null
            && actorState.NativeActorRef is { } actorRef)
        {
            return await NativeJoinActorAsync(
                state,
                spotRid,
                actor,
                actorRef,
                node,
                request,
                cancellationToken).ConfigureAwait(false);
        }

        var joinResult = await spots.JoinActorAsync(
            state,
            spotRid,
            actor,
            request,
            cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult(
            Accepted: joinResult.Accepted,
            ToActorRef(actorState),
            joinResult.Reply ?? Message.From(ReadOnlySpan<byte>.Empty));
    }

    public async ValueTask<ActorRef> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return await _entrySpotJoin.JoinAsync(spotNodeRid, actor, cancellationToken)
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
        out ZLinkActorRuntimeState state)
    {
        return actorSessionManager.TryGetCreatedActorState(actorId, out state);
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
        var reply = await actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
        return reply.Payload;
    }

    internal async ValueTask<ZLinkActorReply> SubmitActorForReplyCoreAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
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

    private async ValueTask<ZLinkActorJoinResult> NativeJoinActorAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        Message request,
        CancellationToken cancellationToken)
    {
        var activation = spots.GetActivationBySpotRid(state, spotRid)
            ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        if (!activation.TryResolveActorJoinDescriptor(out var descriptor) || descriptor is null)
        {
            throw new InvalidOperationException(
                $"SPOT '{activation.SpotRid}' does not declare an actor join callback.");
        }

        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            activation.ChannelName,
            typeof(Message).Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        var joinParts = ZLinkEnvelopeCodec.EncodeParts(joinHeader, request, typeof(Message));

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
        var reply = DecodeNativeJoinReply(
            joinResult.Result,
            replyParts,
            actor.ActorId,
            activation.SpotRid);
        var accepted = joinResult.JoinResultCode == 0;
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var resultActor = accepted ? joinResult.Actor : actorRef;
        if (accepted)
        {
            actorState.NativeActorRef = joinResult.Actor;
            if (joinResult.Actor.NodeRid != actorRef.NodeRid)
            {
                actorState.InvalidateContext();
            }
        }

        return new ZLinkActorJoinResult(
            Accepted: accepted,
            ToActorRef(resultActor),
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

    private static Message DecodeNativeJoinReply(
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
                    "Actor join reply was empty.");
            }

            var reply = (Message)ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(Message))!;
            return Message.From(reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
