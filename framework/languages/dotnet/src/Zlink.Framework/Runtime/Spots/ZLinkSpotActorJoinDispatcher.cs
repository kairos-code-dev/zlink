using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorJoinDispatcher(
    ZLinkFrameworkRuntime runtime,
    IZLinkBackendSpot nativeSpot,
    string channelName,
    ZLinkSpotActorJoinRegistry actorJoins,
    ZLinkSpotActorMembership actors,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker,
    ILogger<ZLinkSpotActorJoinDispatcher>? logger = null)
{
    private readonly ILogger<ZLinkSpotActorJoinDispatcher> _logger =
        logger ?? NullLogger<ZLinkSpotActorJoinDispatcher>.Instance;

    public async ValueTask DispatchAsync(
        ZLinkBackendActorJoinRequest joinRequest,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(joinRequest.Parts);
        if (!actorJoins.TryResolveByName(header.MessageName, out var descriptor) || descriptor is null)
        {
            ReplyRejected(joinRequest, header.MessageName, "no-join-handler", LogLevel.Debug);
            return;
        }

        if (!actors.TryGetActor(joinRequest.TargetActor.ActorId, out var actor) || actor is null)
        {
            actor = runtime.GetOrCreateActorState(joinRequest.TargetActor.ActorId).Actor;
        }

        if (actor is null)
        {
            ReplyRejected(joinRequest, descriptor.MessageName, "no-target-actor", LogLevel.Debug);
            return;
        }

        object requestObj;
        try
        {
            requestObj = ZLinkEnvelopeCodec.DecodeBody(joinRequest.Parts, descriptor.RequestType)!;
        }
        catch (Exception ex)
        {
            ReplyRejected(
                joinRequest,
                descriptor.MessageName,
                "payload-decode-failed",
                LogLevel.Warning,
                ex,
                descriptor.ActorType);
            return;
        }

        object? replyObj;
        try
        {
            replyObj = await handlerInvoker()
                .InvokeActorJoinAsync(descriptor, actor, requestObj, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            ReplyRejected(
                joinRequest,
                descriptor.MessageName,
                "handler-exception",
                LogLevel.Warning,
                ex,
                descriptor.ActorType);
            return;
        }

        var replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
            channelName,
            descriptor.MessageName,
            null,
            replyObj,
            descriptor.ReplyType);
        try
        {
            nativeSpot.ReplyActorJoin(joinRequest, accepted: true, replyParts);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }

    private void ReplyRejected(
        ZLinkBackendActorJoinRequest joinRequest,
        string messageName,
        string reason,
        LogLevel level,
        Exception? exception = null,
        Type? actorType = null)
    {
        ZLinkMessageFlowLogger.Rejected(
            _logger,
            level,
            "EntrySpot",
            "Request",
            messageName,
            reason,
            exception,
            channelName,
            joinRequest.TargetActor.ActorId,
            actorType?.Name,
            joinRequest.TargetSpotRid.ToHex());
        using var emptyReply = Message.FromBytes(ReadOnlySpan<byte>.Empty);
        nativeSpot.ReplyActorJoin(joinRequest, accepted: false, emptyReply);
    }
}
