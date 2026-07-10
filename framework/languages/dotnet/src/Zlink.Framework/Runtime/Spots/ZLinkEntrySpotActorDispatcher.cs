namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
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
                    ZLinkActorBoundSessionRelay.TryReplyMissingNoBindActor(
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

            ZLinkBackendActorRef targetActor;
            bool shouldForward;
            try
            {
                shouldForward = ZLinkActorSessionForwarder.ShouldForward(
                    actorState,
                    frame.Actor,
                    out targetActor);
            }
            catch (ZLinkFrameworkException exception)
                when (exception.Kind == ZLinkFrameworkErrorKind.ActorLocationStale)
            {
                using (frame.Body)
                    await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                            runtime,
                            frame.Actor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            frame.RequestId,
                            frame.Flags,
                            frame.Header,
                            exception,
                            cancellationToken)
                        .ConfigureAwait(false);
                continue;
            }

            if (shouldForward)
            {
                using (frame.Body)
                {
                    ZLinkActorSessionForwarder.Forward(
                        runtime,
                        actorState,
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
                    frame.ReplyActor,
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
        if (ZLinkActorBoundSessionRelay.IsSessionDisconnectedPacket(header))
        {
            ZLinkActorBoundSessionRelay.RemoveNativeBinding(runtime, actor.ActorId, sourceSessionRid);
            if (!await runtime.TryNotifyJoinedSpotActorDisconnectedAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false))
                await runtime.NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false);
            return;
        }

        var boundSession = ZLinkActorBoundSessionRelay.EnterDispatch(
            runtime,
            actor.ActorId,
            sourceNodeRid,
            sourceSessionRid,
            requestId,
            flags);

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
                    await ZLinkActorBoundSessionRelay.SendReplyAsync(
                            runtime,
                            actor.ActorId,
                            actorRef,
                            sourceNodeRid,
                            sourceSessionRid,
                            requestId,
                            flags,
                            boundSession.IsNoBind,
                            header,
                            reply,
                            cancellationToken)
                        .ConfigureAwait(false);

                await boundSession.DrainAsync(cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            await boundSession.DisposeAsync().ConfigureAwait(false);
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

}
