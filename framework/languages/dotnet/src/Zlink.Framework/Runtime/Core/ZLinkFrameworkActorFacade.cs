using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    public async ValueTask<TReply> JoinActorAsync<TRequest, TReply>(
        RoutingId spotRid,
        IZLinkActor actor,
        TRequest request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();

        if (node is not null && actorState.NativeActorRef is { } actorRef)
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

        return await spots.JoinActorAsync<TRequest, TReply>(
            state,
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    public async ValueTask LeaveActorFromSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.LeaveActorFromSpotAsync(activation, actor, cancellationToken);
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
        Message body,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorAsync(actor, header, body, cancellationToken);
    }

    public async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.CreateAndBindActorAsync(actorId, actorType, cancellationToken);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.SubmitActorForReplyAsync(actorId, header, body, cancellationToken);
    }

    public async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorByIdAsync(actorId, header, body, cancellationToken);
    }

    public ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
    {
        return actorSessionManager.GetOrCreateState(actorId);
    }

    private async ValueTask<TReply> NativeJoinActorAsync<TRequest, TReply>(
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
                $"SPOT '{activation.SpotName}' does not register an actor join handler for '{typeof(TRequest)}'.");
        }

        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            activation.ChannelName,
            descriptor.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        var joinParts = ZLinkEnvelopeCodec.EncodeParts(joinHeader, request, typeof(TRequest));

        var tcs = new TaskCompletionSource<(RequestResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<(RequestResult, IReadOnlyList<Message>)>)s!).TrySetCanceled(),
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
                $"Actor join submit failed for '{actor.ActorId}' to SPOT '{activation.SpotName}'.");
        }

        var (joinResult, replyParts) = await tcs.Task.ConfigureAwait(false);

        if (joinResult != RequestResult.Ok)
        {
            foreach (var part in replyParts)
            {
                part.Dispose();
            }

            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor join was rejected for '{actor.ActorId}' to SPOT '{activation.SpotName}'.");
        }

        if (replyParts.Count == 0)
        {
            throw new InvalidOperationException(
                $"Actor join reply for '{typeof(TRequest)}' was empty.");
        }

        try
        {
            var replyObj = ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(TReply));
            return (TReply?)replyObj
                ?? throw new InvalidOperationException($"Actor join reply for '{typeof(TRequest)}' was null.");
        }
        finally
        {
            foreach (var part in replyParts)
            {
                part.Dispose();
            }
        }
    }
}
