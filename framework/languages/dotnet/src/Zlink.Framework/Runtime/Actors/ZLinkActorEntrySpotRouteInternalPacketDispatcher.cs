namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorEntrySpotRouteInternalPacketDispatcher(
    ZLinkFrameworkRuntime runtime)
    : IZLinkRouteInternalPacketDispatcher
{
    public bool CanHandleSend(string packetName)
    {
        _ = packetName;
        return false;
    }

    public bool CanHandleRequest(string packetName)
    {
        return packetName == ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName;
    }

    public ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        _ = received;
        _ = cancellationToken;
        throw new InvalidOperationException("Actor EntrySpot route join does not support fire-and-forget send.");
    }

    public async ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        _ = routedHeader;
        var request = (ZLinkActorEntrySpotRouteJoinRequest?)ZLinkEnvelopeCodec.DecodeBody(
                          received.Parts,
                          typeof(ZLinkActorEntrySpotRouteJoinRequest))
                      ?? throw new InvalidOperationException("Actor EntrySpot route join request was empty.");

        var created = await runtime.CreateLocalActorAsync(
                request.ActorId,
                request.ActorType,
                cancellationToken)
            .ConfigureAwait(false);
        var actor = created.Actor;
        var state = runtime.GetOrCreateActorState(actor.ActorId);
        var nativeRef = state.NativeActorRef
                        ?? throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Actor '{actor.ActorId}' does not have a native Actor ref after EntrySpot route join.");

        var frameworkState = await runtime.GetStartedStateForRoutingAsync(cancellationToken)
            .ConfigureAwait(false);
        var targetNode = FindSpotNode(frameworkState, nativeRef.NodeRid);
        var activation = targetNode.EntrySpotActivation
                         ?? throw new ZLinkFrameworkException(
                             ZLinkFrameworkErrorKind.ActorRouteNotFound,
                             $"Actor EntrySpot route join target node '{nativeRef.NodeRid}' does not have an Entry Spot activation.");

        using var joinRequestPayload = Message.From(request.RequestPayload);
        var joinRequest = ZLinkMessage.FromEnvelopePayload(
            request.RequestContentType,
            joinRequestPayload,
            runtime.Registration.Codecs);
        var admission = activation.TryResolveActorJoin(out var descriptor) && descriptor is not null
            ? await activation.InvokeActorJoinAsync(descriptor, actor, joinRequest, cancellationToken)
                .ConfigureAwait(false)
            : ZLinkSpotActorJoinResult.Reject();
        if (admission.Accepted)
            await runtime.NotifyEntrySpotActorJoinedAsync(
                    actor,
                    nativeRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);

        var replyContentType = ZLinkEnvelopeCodec.DefaultContentType;
        Message? replyPayload = null;
        if (admission.Reply is { } reply)
        {
            var encodedReply = reply.Encode(runtime.Registration.Codecs);
            replyContentType = encodedReply.ContentType;
            replyPayload = Message.From(encodedReply.Payload.Bytes.Span);
        }

        using (replyPayload)
        {
            return ZLinkEnvelopeCodec.EncodePart(new ZLinkActorEntrySpotRouteJoinReply(
                admission.Accepted,
                actor.ActorId,
                state.ActorType ?? request.ActorType,
                nativeRef.NodeRid.ToHex(),
                nativeRef.Generation,
                replyContentType,
                replyPayload?.ToArray() ?? Array.Empty<byte>()));
        }
    }

    private static ZLinkSpotNodeRuntime FindSpotNode(
        ZLinkFrameworkRuntimeState state,
        RoutingId nodeRid)
    {
        foreach (var candidate in state.SpotNodes.Values)
            if (candidate.Node.RoutingId == nodeRid)
                return candidate;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor EntrySpot route join target node '{nodeRid}' is not registered.");
    }
}