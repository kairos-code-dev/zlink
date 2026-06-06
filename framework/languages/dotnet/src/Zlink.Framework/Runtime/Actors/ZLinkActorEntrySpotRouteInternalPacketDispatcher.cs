using Zlink.Framework.Runtime.Channels;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;

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

        await runtime.NotifyEntrySpotActorJoinedAsync(
                actor,
                nativeRef.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);

        return ZLinkEnvelopeCodec.EncodePart(new ZLinkActorEntrySpotRouteJoinReply(
            actor.ActorId,
            state.ActorType ?? request.ActorType,
            nativeRef.NodeRid.ToHex(),
            nativeRef.Generation));
    }
}
