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
        if (!actorJoins.TryResolve(out var descriptor) || descriptor is null)
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
            ReplyRejected(joinRequest, header.MessageName, "no-target-actor", LogLevel.Debug);
            return;
        }

        Message request;
        try
        {
            if (joinRequest.Parts.Count <= 1)
            {
                throw new InvalidOperationException("Actor join request body part is missing.");
            }

            request = joinRequest.Parts[1];
        }
        catch (Exception ex)
        {
            ReplyRejected(
                joinRequest,
                header.MessageName,
                "payload-decode-failed",
                LogLevel.Warning,
                ex,
                descriptor.ActorType);
            return;
        }

        ZLinkSpotActorJoinResult result;
        try
        {
            result = await handlerInvoker()
                .InvokeActorJoinAsync(descriptor, actor, request, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            ReplyRejected(
                joinRequest,
                header.MessageName,
                "handler-exception",
                LogLevel.Warning,
                ex,
                descriptor.ActorType);
            return;
        }

        var replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
            channelName,
            header.MessageName,
            null,
            result.Reply,
            typeof(Message));
        try
        {
            nativeSpot.ReplyActorJoin(
                joinRequest,
                joinResultCode: result.Accepted ? 0 : 1,
                replyParts);
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
        using var emptyReply = Message.From(ReadOnlySpan<byte>.Empty);
        nativeSpot.ReplyActorJoin(joinRequest, joinResultCode: 1, emptyReply);
    }
}
