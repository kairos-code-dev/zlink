namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkRemoteActorJoinPackets
{
    public const string RequestPacketName = "__zlink.actor.join_spot.request";
    public const string AdmissionPacketName = "__zlink.actor.join_spot.admission";
    public const string CommitPacketName = "__zlink.actor.join_spot.commit";
    public const string HandoffCompletionPacketName = "__zlink.actor.join_spot.handoff_completion";
    public const string BoundSessionBindPacketName = "zlink.framework.actor.bound_session.bind";
    public const string SessionDisconnectedPacketName = "zlink.framework.actor.session_disconnected";

    public static IReadOnlyList<Message> EncodeAdmissionRequest(
        ZLinkEnvelopeHeader header,
        string actorId,
        string actorType,
        string handoffId,
        DateTimeOffset deadline,
        string sourceSpotId,
        RoutingId sourceNodeRid,
        ZLinkMessage request,
        ZLinkCodecRegistryBuilder codecs)
    {
        var encodedRequest = request.Encode(codecs);
        var payload = new ZLinkRemoteActorAdmissionRequest(
            actorId,
            actorType,
            ZLinkSpotId.Require(sourceSpotId, nameof(sourceSpotId)),
            sourceNodeRid.ToBytes().ToArray(),
            encodedRequest.ContentType,
            encodedRequest.Payload.ToArray(),
            handoffId,
            deadline.ToUnixTimeMilliseconds());

        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            payload,
            typeof(ZLinkRemoteActorAdmissionRequest),
            null);
    }

    public static IReadOnlyList<Message> EncodeJoinRequest(
        ZLinkEnvelopeHeader header,
        string actorId,
        string actorType,
        string handoffId,
        string sourceSpotId,
        RoutingId sourceNodeRid,
        RoutingId? boundSessionNodeRid,
        RoutingId boundSessionRid,
        ZLinkMessage transferState,
        ZLinkMessage request,
        IReadOnlyList<ZLinkActorHandoffFrame> handoffFrames,
        ZLinkCodecRegistryBuilder codecs)
    {
        var encodedRequest = request.Encode(codecs);
        var encodedTransferState = transferState.Encode(codecs);
        var payload = new ZLinkRemoteActorJoinRequest(
            actorId,
            actorType,
            handoffId,
            boundSessionNodeRid?.ToBytes().ToArray(),
            boundSessionRid.Size > 0 ? boundSessionRid.ToBytes().ToArray() : null,
            encodedTransferState.ContentType,
            encodedTransferState.Payload.ToArray(),
            encodedRequest.ContentType,
            encodedRequest.Payload.ToArray(),
            handoffFrames,
            ZLinkSpotId.Require(sourceSpotId, nameof(sourceSpotId)),
            sourceNodeRid.ToBytes().ToArray());

        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            payload,
            typeof(ZLinkRemoteActorJoinRequest),
            null);
    }

    public static ZLinkRemoteActorJoinRequest DecodeJoinRequest(IReadOnlyList<Message> parts)
    {
        return (ZLinkRemoteActorJoinRequest?)ZLinkEnvelopeCodec.DecodeBody(
                   parts,
                   typeof(ZLinkRemoteActorJoinRequest))
               ?? throw new InvalidOperationException("Remote actor join request was empty.");
    }

    public static IReadOnlyList<Message> EncodeHandoffCompletionRequest(
        ZLinkEnvelopeHeader header,
        string actorId,
        string handoffId,
        string sourceSpotId,
        RoutingId sourceNodeRid,
        string targetSpotId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            new ZLinkRemoteActorHandoffCompletionRequest(
                actorId,
                handoffId,
                ZLinkSpotId.Require(sourceSpotId, nameof(sourceSpotId)),
                sourceNodeRid.ToBytes().ToArray(),
                ZLinkSpotId.Require(targetSpotId, nameof(targetSpotId)),
                frames),
            typeof(ZLinkRemoteActorHandoffCompletionRequest),
            null);
    }

    public static ZLinkRemoteActorHandoffCompletionRequest DecodeHandoffCompletionRequest(
        IReadOnlyList<Message> parts)
    {
        return (ZLinkRemoteActorHandoffCompletionRequest?)ZLinkEnvelopeCodec.DecodeBody(
                   parts,
                   typeof(ZLinkRemoteActorHandoffCompletionRequest))
               ?? throw new InvalidOperationException("Remote actor handoff completion request was empty.");
    }

    public static ZLinkRemoteActorAdmissionRequest DecodeAdmissionRequest(IReadOnlyList<Message> parts)
    {
        return (ZLinkRemoteActorAdmissionRequest?)ZLinkEnvelopeCodec.DecodeBody(
                   parts,
                   typeof(ZLinkRemoteActorAdmissionRequest))
               ?? throw new InvalidOperationException("Remote actor admission request was empty.");
    }

    public static ZLinkRemoteActorBoundSessionRoute DecodeBoundSessionRoute(ZLinkRemoteActorJoinRequest request)
    {
        return new ZLinkRemoteActorBoundSessionRoute(
            ToRoutingId(request.BoundSessionNodeRid),
            ToRoutingId(request.BoundSessionRid));
    }

    public static ZLinkMessage DecodeJoinRequestPayload(
        ZLinkRemoteActorJoinRequest request,
        ZLinkCodecRegistryBuilder codecs)
    {
        return DecodeJoinRequestPayload(
            request.RequestContentType,
            request.Request,
            codecs);
    }

    public static ZLinkMessage DecodeAdmissionRequestPayload(
        ZLinkRemoteActorAdmissionRequest request,
        ZLinkCodecRegistryBuilder codecs)
    {
        return DecodeJoinRequestPayload(
            request.RequestContentType,
            request.Request,
            codecs);
    }

    public static ZLinkMessage DecodeTransferState(
        ZLinkRemoteActorJoinRequest request,
        ZLinkCodecRegistryBuilder codecs)
    {
        return DecodeJoinRequestPayload(
            request.TransferStateContentType,
            request.TransferState,
            codecs);
    }

    private static ZLinkMessage DecodeJoinRequestPayload(
        string requestContentType,
        byte[] requestPayload,
        ZLinkCodecRegistryBuilder codecs)
    {
        using var payload = Message.From(requestPayload);
        return ZLinkMessage.FromEnvelopePayload(
            requestContentType,
            payload,
            codecs);
    }

    public static ZLinkRemoteActorJoinReply CreateJoinReply(
        bool accepted,
        ZLinkBackendActorRef actorRef)
    {
        return new ZLinkRemoteActorJoinReply(
            accepted,
            actorRef.NodeRid.ToBytes().ToArray(),
            actorRef.ActorId,
            actorRef.Generation);
    }

    public static ZLinkRemoteActorAdmissionReply CreateAdmissionReply(
        bool accepted,
        ZLinkMessage? reply,
        ZLinkCodecRegistryBuilder codecs,
        long deadlineUnixTimeMilliseconds = 0)
    {
        var replyContentType = ZLinkEnvelopeCodec.DefaultContentType;
        Message? encodedReply = null;
        if (reply is not null)
        {
            var encoded = reply.Encode(codecs);
            replyContentType = encoded.ContentType;
            encodedReply = Message.From(encoded.Payload.Bytes.Span);
        }

        using (encodedReply)
        {
            return new ZLinkRemoteActorAdmissionReply(
                accepted,
                replyContentType,
                encodedReply?.ToArray() ?? Array.Empty<byte>(),
                deadlineUnixTimeMilliseconds);
        }
    }

    public static IReadOnlyList<Message> EncodeJoinReplyEnvelope(
        string channelName,
        string messageName,
        string? correlationId,
        ZLinkRemoteActorJoinReply reply)
    {
        return ZLinkSpotReplyEnvelope.EncodeResponseParts(
            channelName,
            messageName,
            correlationId,
            reply,
            typeof(ZLinkRemoteActorJoinReply));
    }

    public static ZLinkRemoteActorJoinReply DecodeJoinReplyAndDispose(
        IReadOnlyList<Message> parts,
        string actorId,
        string targetSpotId)
    {
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<ZLinkRemoteActorJoinReply>(
            parts,
            "Remote actor join reply was empty.",
            $"Remote actor join failed for '{actorId}' to SPOT '{targetSpotId}'.",
            null);
    }

    public static ZLinkRemoteActorAdmissionReply DecodeAdmissionReplyAndDispose(
        IReadOnlyList<Message> parts,
        string actorId,
        string targetSpotId)
    {
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<ZLinkRemoteActorAdmissionReply>(
            parts,
            "Remote actor admission reply was empty.",
            $"Remote actor admission failed for '{actorId}' to SPOT '{targetSpotId}'.",
            null);
    }

    public static ZLinkMessage DecodeAdmissionReplyPayload(
        ZLinkRemoteActorAdmissionReply reply,
        ZLinkCodecRegistryBuilder codecs)
    {
        using var payload = Message.From(reply.Reply);
        return ZLinkMessage.FromEnvelopePayload(
            reply.ReplyContentType,
            payload,
            codecs);
    }

    public static ZLinkBackendActorRef ToActorRef(ZLinkRemoteActorJoinReply reply)
    {
        return new ZLinkBackendActorRef(
            RoutingId.From(reply.ActorNodeRid),
            reply.ActorId,
            reply.ActorGeneration);
    }

    private static RoutingId? ToRoutingId(byte[]? bytes)
    {
        return bytes is { Length: > 0 } ? RoutingId.From(bytes) : null;
    }
}

internal readonly record struct ZLinkRemoteActorBoundSessionRoute(
    RoutingId? NodeRid,
    RoutingId? SessionRid);

internal sealed record ZLinkRemoteActorAdmissionRequest(
    string ActorId,
    string ActorType,
    string SourceSpotId,
    byte[] SourceNodeRid,
    string RequestContentType,
    byte[] Request,
    string HandoffId = "",
    long DeadlineUnixTimeMilliseconds = 0);

internal sealed record ZLinkRemoteActorAdmissionReply(
    bool Accepted,
    string ReplyContentType,
    byte[] Reply,
    long DeadlineUnixTimeMilliseconds = 0);

internal sealed record ZLinkRemoteActorJoinRequest(
    string ActorId,
    string ActorType,
    string HandoffId,
    byte[]? BoundSessionNodeRid,
    byte[]? BoundSessionRid,
    string TransferStateContentType,
    byte[] TransferState,
    string RequestContentType,
    byte[] Request,
    IReadOnlyList<ZLinkActorHandoffFrame> HandoffFrames,
    string SourceSpotId,
    byte[] SourceNodeRid);

internal sealed record ZLinkRemoteActorJoinReply(
    bool Accepted,
    byte[] ActorNodeRid,
    string ActorId,
    ulong ActorGeneration);

internal sealed record ZLinkRemoteActorHandoffCompletionRequest(
    string ActorId,
    string HandoffId,
    string SourceSpotId,
    byte[] SourceNodeRid,
    string TargetSpotId,
    IReadOnlyList<ZLinkActorHandoffFrame> Frames);
