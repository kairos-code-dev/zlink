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
        var request = ZLinkActorEntrySpotRoutePackets.DecodeJoinRequest(received.Parts);

        if (!runtime.DrainAdmission.TryEnterActorAdmission(out var admissionLease))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateRejected,
                "The framework runtime is draining and does not accept new actor assignments.",
                false);
        using (admissionLease)
        {

        // Hosting handoff from the source node (see JoinRoutedActorAsync).
        var created = await runtime.CreateLocalActorForHandoffAsync(
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

        var joinRequest = ZLinkActorEntrySpotRoutePackets.DecodeJoinRequestPayload(
            request,
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

        return ZLinkActorEntrySpotRoutePackets.EncodeJoinReply(
            admission.Accepted,
            actor.ActorId,
            state.ActorType ?? request.ActorType,
            nativeRef,
            admission.Reply,
            runtime.Registration.Codecs);
        }
    }

    private static ZLinkSpotNodeRuntime FindSpotNode(
        ZLinkFrameworkRuntimeState state,
        RoutingId nodeRid)
    {
        if (state.TryGetSpotNodeByRoutingId(nodeRid, out var nodeRuntime))
            return nodeRuntime;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor EntrySpot route join target node '{nodeRid}' is not registered.");
    }
}
