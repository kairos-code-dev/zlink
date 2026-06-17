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
        var payload = DecodeJoinPayload(joinRequest);
        if (!actorJoins.TryResolve(out var descriptor) || descriptor is null)
        {
            ReplyRejected(joinRequest, payload.MessageName, "no-join-handler", LogLevel.Debug);
            return;
        }

        if (!actors.TryGetActor(joinRequest.TargetActor.ActorId, out var actor) || actor is null)
        {
            actor = runtime.GetOrCreateActorState(joinRequest.TargetActor.ActorId).Actor;
        }

        if (actor is null)
        {
            ReplyRejected(joinRequest, payload.MessageName, "no-target-actor", LogLevel.Debug);
            return;
        }

        if (payload.Error is { } payloadError)
        {
            ReplyRejected(
                joinRequest,
                payload.MessageName,
                "payload-decode-failed",
                LogLevel.Warning,
                payloadError,
                descriptor.ActorType);
            return;
        }

        ZLinkSpotActorJoinResult result;
        try
        {
            result = await handlerInvoker()
                .InvokeActorJoinAsync(descriptor, actor, payload.Request, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            ReplyRejected(
                joinRequest,
                payload.MessageName,
                "handler-exception",
                LogLevel.Warning,
                ex,
                descriptor.ActorType);
            return;
        }

        if (!payload.UsesEnvelope)
        {
            if (result.Reply is { } reply)
            {
                nativeSpot.ReplyActorJoin(
                    joinRequest,
                    joinResultCode: result.Accepted ? 0 : 1,
                    reply);
                return;
            }

            using var emptyReply = Message.From(ReadOnlySpan<byte>.Empty);
            nativeSpot.ReplyActorJoin(
                joinRequest,
                joinResultCode: result.Accepted ? 0 : 1,
                emptyReply);
            return;
        }

        var replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
            channelName,
            payload.MessageName,
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

    private static JoinPayload DecodeJoinPayload(ZLinkBackendActorJoinRequest joinRequest)
    {
        if (joinRequest.Parts.Count == 1)
        {
            return new JoinPayload(
                UsesEnvelope: false,
                MessageName: typeof(Message).Name,
                Request: joinRequest.Parts[0],
                Error: null);
        }

        try
        {
            var header = ZLinkEnvelopeCodec.DecodeHeader(joinRequest.Parts);
            if (joinRequest.Parts.Count <= 1)
            {
                throw new InvalidOperationException("Actor join request body part is missing.");
            }

            return new JoinPayload(
                UsesEnvelope: true,
                header.MessageName,
                joinRequest.Parts[1],
                Error: null);
        }
        catch (Exception ex)
        {
            return new JoinPayload(
                UsesEnvelope: true,
                MessageName: typeof(Message).Name,
                Request: Message.From(ReadOnlySpan<byte>.Empty),
                Error: ex);
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

    private readonly record struct JoinPayload(
        bool UsesEnvelope,
        string MessageName,
        Message Request,
        Exception? Error);
}
