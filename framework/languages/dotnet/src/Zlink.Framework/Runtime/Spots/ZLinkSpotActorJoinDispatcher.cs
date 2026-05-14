using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorJoinDispatcher(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpot nativeSpot,
    string channelName,
    ZLinkSpotActorJoinRegistry actorJoins,
    ZLinkSpotActorMembership actors,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
{
    public async ValueTask DispatchAsync(
        ZLinkBackendActorJoinRequest joinRequest,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(joinRequest.Parts);
        if (!actorJoins.TryResolveByName(header.MessageName, out var descriptor) || descriptor is null)
        {
            ReplyRejected(joinRequest);
            return;
        }

        if (!actors.TryGetActor(joinRequest.TargetActor.ActorId, out var actor) || actor is null)
        {
            actor = runtime.GetOrCreateActorState(joinRequest.TargetActor.ActorId).Actor;
        }

        if (actor is null)
        {
            ReplyRejected(joinRequest);
            return;
        }

        var requestObj = ZLinkEnvelopeCodec.DecodeBody(joinRequest.Parts, descriptor.RequestType)!;
        var replyObj = await handlerInvoker()
            .InvokeActorJoinAsync(descriptor, actor, requestObj, cancellationToken)
            .ConfigureAwait(false);

        var replyEnvelopeHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            channelName,
            descriptor.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null, null, null, null, null);
        var replyParts = ZLinkEnvelopeCodec.EncodeParts(replyEnvelopeHeader, replyObj, descriptor.ReplyType);
        try
        {
            nativeSpot.ReplyActorJoin(joinRequest, accepted: true, replyParts);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }

    private void ReplyRejected(ZLinkBackendActorJoinRequest joinRequest)
    {
        using var emptyReply = Message.FromBytes(ReadOnlySpan<byte>.Empty);
        nativeSpot.ReplyActorJoin(joinRequest, accepted: false, emptyReply);
    }
}
