
using Zlink.Framework.Runtime.Streams;

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
        int i = 0;
        while (i < parts.Count)
        {
            var headerPart = parts[i++];
            var actorState = runtime.GetOrCreateActorState(headerPart.Actor.ActorId);
            var actor = actorState.Actor;
            if (actor is null)
            {
                ZLinkSpotActorFrameReader.DisposeFrame(parts, ref i, headerPart);
                continue;
            }

            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref i, headerPart, out var frame))
            {
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
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
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
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
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
                    sourceNodeRid,
                    sourceSessionRid,
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
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await using var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actor.ActorId);
        runtime.BindActorSession(
            actor.ActorId,
            sourceNodeRid,
            sourceSessionRid,
            BuildNativeBoundSessionToken(sourceSessionRid));

        if (header.RequestSeq is not null)
        {
            var result = await runtime.TrySubmitEntrySpotActorForReplyAsync(
                    actor,
                    actorState,
                    header,
                    body,
                    cancellationToken)
                .ConfigureAwait(false);
            var reply = result.Handled
                ? result.Reply
                : await runtime.SubmitActorForReplyAsync(
                        actor.ActorId,
                        header,
                        body,
                        cancellationToken)
                    .ConfigureAwait(false);

            if (reply is not null)
            {
                await SendResponseAsync(runtime, actor.ActorId, header, reply, cancellationToken)
                    .ConfigureAwait(false);
            }

            await boundSessionScope.DrainAsync(cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        if (activation is not null
            && activation.TryResolveActorPacket(actor.GetType(), header, out var descriptor)
            && descriptor is not null)
        {
            await runtime.SubmitResolvedEntrySpotActorAsync(
                    actor,
                    actorState,
                    header,
                    ct => activation.InvokeActorPacketAsync(
                        descriptor,
                        actor,
                        header,
                        body,
                        ct),
                    cancellationToken)
                .ConfigureAwait(false);

            return;
        }

        await runtime.SubmitActorAsync(actor, header, body, cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask SendResponseAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        ZlinkStreamHeader requestHeader,
        ZLinkActorReply reply,
        CancellationToken cancellationToken)
    {
        var frame = reply.ToFrame(requestHeader);
        await SendFrameWithRetryAsync(runtime, actorId, frame, cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask SendFrameWithRetryAsync(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var timeout = runtime.Registration.DefaultRequestTimeout;
        var retryDelay = TimeSpan.FromMilliseconds(25);
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
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
                {
                    return;
                }
            }
            catch (ZlinkSubmitException error) when (error.Result == ZlinkSubmitException.ErrorCode.NotConnected)
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
            {
                throw new InvalidOperationException("Actor request reply relay failed.", lastError);
            }

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
