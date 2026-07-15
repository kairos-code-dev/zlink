using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkActorBoundSessionRelay
{
    private const uint ActorRecvInfoNoBind = 1u;

    public static bool IsSessionDisconnectedPacket(ZlinkStreamHeader header)
    {
        return string.Equals(
            header.Name,
            ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
            StringComparison.Ordinal);
    }

    public static void RemoveNativeBinding(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sourceSessionRid)
    {
        runtime.RemoveActorSessionBinding(actorId, ZLinkActorBoundSessionBindingToken.Native(sourceSessionRid));
    }

    public static ZLinkActorBoundSessionDispatch EnterDispatch(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags)
    {
        var isNoBind = IsNoBindRequest(requestId, flags);
        var scope = ZLinkBoundSessionDispatchScope.Enter(actorId);
        if (!isNoBind)
            runtime.BindActorSession(
                actorId,
                sourceNodeRid,
                sourceSessionRid,
                ZLinkActorBoundSessionBindingToken.Native(sourceSessionRid));

        return new ZLinkActorBoundSessionDispatch(isNoBind, scope);
    }

    public static bool TryReplyMissingNoBindActor(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader requestHeader)
    {
        if (requestHeader.Kind != ZlinkStreamMessageKind.Request
            || requestHeader.RequestSeq is null
            || !IsNoBindRequest(requestId, flags))
            return false;

        ReplyNoBind(
            runtime,
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags,
            requestHeader,
            ZLinkActorReply.FromError(new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorRef.ActorId}' is not available.")));
        return true;
    }

    public static async ValueTask SendReplyAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        bool isNoBind,
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        if (isNoBind)
        {
            ReplyNoBind(
                runtime,
                actorRef,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                requestHeader,
                reply);
            return;
        }

        var frame = reply.ToFrame(requestHeader);
        await SendFrameWithRetryAsync(runtime, actorId, sourceSessionRid, frame, cancellationToken)
            .ConfigureAwait(false);
    }

    public static async ValueTask ReplyStaleActorAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader requestHeader,
        ZLinkFrameworkException exception,
        CancellationToken cancellationToken)
    {
        if (requestHeader.Kind != ZlinkStreamMessageKind.Request
            || requestHeader.RequestSeq is null)
            return;

        var dispatch = EnterDispatch(
            runtime,
            actorRef.ActorId,
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags);
        try
        {
            await SendReplyAsync(
                    runtime,
                    actorRef.ActorId,
                    actorRef,
                    sourceNodeRid,
                    sourceSessionRid,
                    requestId,
                    flags,
                    dispatch.IsNoBind,
                    requestHeader,
                    ZLinkActorReply.FromError(exception),
                    cancellationToken)
                .ConfigureAwait(false);
            await dispatch.DrainAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            await dispatch.DisposeAsync().ConfigureAwait(false);
        }
    }

    private static void ReplyNoBind(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply)
    {
        var frame = reply.ToFrame(requestHeader);
        using var replyMessage = Message.From(frame);
        runtime.ReplyActorNoBind(
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags,
            [replyMessage]);
        runtime.LogActorHandoff(
            $"request_reply_direct actor={actorRef.ActorId} request_id={requestId} caller_node={sourceNodeRid}");
    }

    private static async ValueTask SendFrameWithRetryAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        RoutingId sourceSessionRid,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var sourceBindingToken = ZLinkActorBoundSessionBindingToken.Native(sourceSessionRid);
        await ZLinkRetryingSubmitter.Async(
                () =>
                {
                    using var frameMessage = Message.From(frame);
                    return runtime.SendActorBoundSessionIfCurrent(
                        actorId,
                        sourceBindingToken,
                        new[] { frameMessage },
                        SendFlags.None);
                },
                runtime.Registration.DefaultRequestTimeout,
                "Actor request reply relay failed.",
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static bool IsNoBindRequest(ulong requestId, uint flags)
    {
        return requestId != 0 && (flags & ActorRecvInfoNoBind) != 0;
    }

}

internal readonly struct ZLinkActorBoundSessionDispatch(
    bool isNoBind,
    ZLinkBoundSessionDispatchScope scope) : IAsyncDisposable
{
    public bool IsNoBind => isNoBind;

    public ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        return scope.DrainAsync(cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        return scope.DisposeAsync();
    }
}
