
namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkEntrySpotActorDispatcher
{
    private static readonly int MaxConcurrentDispatches = Math.Max(1, Environment.ProcessorCount);

    public static async Task DispatchAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        CancellationToken cancellationToken)
    {
        var dispatchTasks = new ZLinkBoundedTaskSet(MaxConcurrentDispatches);
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

            await dispatchTasks.AddAsync(
                    DispatchPacketAndDisposeBodyAsync(
                        runtime,
                        activation,
                        actorState,
                        actor,
                        frame.SourceSessionRid,
                        frame.Header,
                        frame.Body,
                        cancellationToken))
                .ConfigureAwait(false);
        }

        await dispatchTasks.DrainAsync().ConfigureAwait(false);
    }

    private static async Task DispatchPacketAndDisposeBodyAsync(
        ZLinkFrameworkRuntime runtime,
        ZLinkEntrySpotActivation? activation,
        ZLinkActorRuntimeState actorState,
        IZLinkActor actor,
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
        RoutingId sourceSessionRid,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        await using var boundSessionScope = ZLinkBoundSessionDispatchScope.Enter(actor.ActorId);
        runtime.BindActorSession(
            actor.ActorId,
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
                SendResponse(runtime, actor.ActorId, header, reply);
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

    private static void SendResponse(
        ZLinkFrameworkRuntime runtime,
        string actorId,
        ZlinkStreamHeader requestHeader,
        byte[] reply)
    {
        var responseHeader = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            requestHeader.Codec,
            ZlinkStreamHeaderFlags.HasRequestSeq,
            requestHeader.RequestSeq,
            requestHeader.Name,
            ZlinkStreamMetadata.Empty);
        var frame = ZLinkStreamFrameCodec.Encode(
            ZLinkStreamProtocolDefaults.EncodeHeader(responseHeader).Span,
            reply);
        using var frameMessage = Message.FromBytes(frame);
        if (!runtime.SendActorBoundSession(
                actorId,
                new[] { frameMessage },
                SendFlags.None))
        {
            throw new InvalidOperationException("Actor request reply relay failed.");
        }
    }

    private static string BuildNativeBoundSessionToken(RoutingId sourceSessionRid)
    {
        return $"native:{sourceSessionRid.ToHex()}";
    }
}
