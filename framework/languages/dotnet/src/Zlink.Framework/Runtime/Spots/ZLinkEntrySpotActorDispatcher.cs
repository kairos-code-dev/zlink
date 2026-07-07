using System.Diagnostics;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
    private const uint ActorRecvInfoNoBind = 1u;

    public static async Task DispatchAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        // Entry Spot packets execute one at a time in arrival order. The
        // handler invocation itself is gated on the Entry Spot serial
        // execution line; awaiting each packet here keeps the enqueue order
        // identical to the native batch order.
        var i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            var actorState = runtime.GetOrCreateActorState(headerPart.Actor.ActorId);
            var actor = actorState.Actor;
            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame)) continue;

            if (actor is null)
            {
                using (frame.Body)
                {
                    ReplyNoBindActorMissingIfRequest(
                        runtime,
                        frame.Actor,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.RequestId,
                        frame.Flags,
                        frame.Header);
                }

                continue;
            }

            if (ZLinkActorSessionForwarder.ShouldForward(
                    actorState,
                    frame.Actor,
                    out var targetActor))
            {
                using (frame.Body)
                {
                    ZLinkActorSessionForwarder.Forward(
                        runtime,
                        targetActor,
                        frame.SourceNodeRid,
                        frame.SourceSessionRid,
                        frame.Header,
                        frame.Body);
                }

                continue;
            }

            await DispatchPacketAndDisposeBodyAsync(
                    runtime,
                    activation,
                    actorState,
                    actor,
                    frame.Actor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.Header,
                    frame.Body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static async Task DispatchPacketAndDisposeBodyAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        ZLinkActorRuntimeState actorState,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        try
        {
            await DispatchPacketAsync(
                    runtime,
                    activation,
                    actorState,
                    actor,
                    actorRef,
                    sourceNodeRid,
                    sourceSessionRid,
                    requestId,
                    flags,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            body.Dispose();
        }
    }

    private static async ValueTask DispatchPacketAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        ZLinkActorRuntimeState actorState,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (string.Equals(
                header.Name,
                ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
                StringComparison.Ordinal))
        {
            runtime.RemoveActorSessionBinding(actor.ActorId, BuildNativeBoundSessionToken(sourceSessionRid));
            if (!await runtime.TryNotifyJoinedSpotActorDisconnectedAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false))
                await runtime.NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false);
            return;
        }

        var isNoBind = IsNoBindRequest(requestId, flags);
        var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actor.ActorId);
        if (!isNoBind)
        {
            runtime.BindActorSession(
                actor.ActorId,
                sourceNodeRid,
                sourceSessionRid,
                BuildNativeBoundSessionToken(sourceSessionRid));
        }

        try
        {
            // Only genuine requests take the reply path: a relayed Send can
            // carry a request seq (stream-level bookkeeping), and treating it
            // as a request dead-ends in a reply lookup that re-enters this
            // actor's dispatch turn.
            if (header.Kind == ZlinkStreamMessageKind.Request && header.RequestSeq is not null)
            {
                var liveActivation = actorState.LiveActivation;
                var reply = liveActivation is not null
                    ? await runtime.SubmitActorForReplyAsync(
                            actor.ActorId,
                            header,
                            body,
                            cancellationToken)
                        .ConfigureAwait(false)
                    : await SubmitEntryOrCurrentActorForReplyAsync(
                            runtime,
                            actor,
                            actorState,
                            header,
                            body,
                            cancellationToken)
                        .ConfigureAwait(false);

                if (reply is not null)
                    await SendResponseAsync(
                            runtime,
                            actor.ActorId,
                            actorRef,
                            sourceNodeRid,
                            sourceSessionRid,
                            requestId,
                            flags,
                            isNoBind,
                            header,
                            reply,
                            cancellationToken)
                        .ConfigureAwait(false);

                await boundSessionScope.DrainAsync(cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            if (actorState.LiveActivation is not null)
            {
                await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            await boundSessionScope.DisposeAsync().ConfigureAwait(false);
        }
    }

    private static async ValueTask<ZLinkActorReply?> SubmitEntryOrCurrentActorForReplyAsync(
        ZLinkFrameworkRuntime runtime,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        var result = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                actor,
                actorState,
                header,
                body,
                callerOwnsDispatchTurn: false,
                cancellationToken)
            .ConfigureAwait(false);
        return result.Handled
            ? result.Reply
            : await runtime.SubmitActorForReplyAsync(
                    actor.ActorId,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
    }

    private static async ValueTask SendResponseAsync(
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
            SendNoBindReply(
                runtime,
                actorRef,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                requestHeader,
                reply);
        }
        else
        {
            var frame = reply.ToFrame(requestHeader);
            await SendFrameWithRetryAsync(runtime, actorId, frame, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static void ReplyNoBindActorMissingIfRequest(
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
            return;

        SendNoBindReply(
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
    }

    private static void SendNoBindReply(
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
    }

    private static bool IsNoBindRequest(ulong requestId, uint flags)
    {
        return requestId != 0 && (flags & ActorRecvInfoNoBind) != 0;
    }

    private static async ValueTask SendFrameWithRetryAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var timeout = runtime.Registration.DefaultRequestTimeout;
        var retryDelay = TimeSpan.FromMilliseconds(25);
        var elapsed = Stopwatch.StartNew();
        Exception? lastError = null;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            using var frameMessage = Message.From(frame);
            try
            {
                if (runtime.SendActorBoundSession(
                        actorId,
                        new[] { frameMessage },
                        SendFlags.None))
                    return;
            }
            catch (ZlinkSubmitException error) when (error.Result == ZlinkSubmitException.ErrorCode.NotConnected)
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
                throw new InvalidOperationException("Actor request reply relay failed.", lastError);

            var remaining = timeout - elapsed.Elapsed;
            await Task.Delay(remaining < retryDelay ? remaining : retryDelay, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static string BuildNativeBoundSessionToken(RoutingId sourceSessionRid)
    {
        return $"native:{sourceSessionRid.ToHex()}";
    }
}
