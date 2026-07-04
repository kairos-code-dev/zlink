namespace Zlink.Framework.Runtime.Actors;

internal static class ZLinkRemoteActorJoinPackets
{
    public const string RequestPacketName = "__zlink.actor.join_spot.request";
    public const string BoundSessionBindPacketName = "zlink.framework.actor.bound_session.bind";
    public const string SessionDisconnectedPacketName = "zlink.framework.actor.session_disconnected";

    public static IReadOnlyList<Message> EncodeJoinRequest(
        ZLinkEnvelopeHeader header,
        string actorId,
        string actorType,
        RoutingId? boundSessionNodeRid,
        RoutingId boundSessionRid,
        ZLinkMessage request,
        ZLinkCodecRegistryBuilder codecs)
    {
        var encodedRequest = request.Encode(codecs);
        var payload = new ZLinkRemoteActorJoinRequest(
            actorId,
            actorType,
            boundSessionNodeRid?.ToBytes().ToArray(),
            boundSessionRid.Size > 0 ? boundSessionRid.ToBytes().ToArray() : null,
            encodedRequest.ContentType,
            encodedRequest.Payload.ToArray());

        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            payload,
            typeof(ZLinkRemoteActorJoinRequest));
    }

    public static ZLinkRemoteActorJoinRequest DecodeJoinRequest(IReadOnlyList<Message> parts)
    {
        return (ZLinkRemoteActorJoinRequest?)ZLinkEnvelopeCodec.DecodeBody(
                   parts,
                   typeof(ZLinkRemoteActorJoinRequest))
               ?? throw new InvalidOperationException("Remote actor join request was empty.");
    }

    public static string GetJoinRequestActorId(ZLinkRemoteActorJoinRequest request)
    {
        return request.ActorId;
    }

    public static string GetJoinRequestActorType(ZLinkRemoteActorJoinRequest request)
    {
        return request.ActorType;
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
        ZLinkBackendActorRef actorRef,
        ZLinkMessage? reply,
        ZLinkCodecRegistryBuilder codecs)
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
            return new ZLinkRemoteActorJoinReply(
                accepted,
                actorRef.NodeRid.ToBytes().ToArray(),
                actorRef.ActorId,
                actorRef.Generation,
                replyContentType,
                encodedReply?.ToArray() ?? Array.Empty<byte>());
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
        RoutingId targetSpotRid)
    {
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<ZLinkRemoteActorJoinReply>(
            parts,
            "Remote actor join reply was empty.",
            $"Remote actor join failed for '{actorId}' to SPOT '{targetSpotRid}'.");
    }

    public static ZLinkMessage DecodeJoinReplyPayload(
        ZLinkRemoteActorJoinReply reply,
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

internal sealed record ZLinkRemoteActorJoinRequest(
    string ActorId,
    string ActorType,
    byte[]? BoundSessionNodeRid,
    byte[]? BoundSessionRid,
    string RequestContentType,
    byte[] Request);

internal sealed record ZLinkRemoteActorJoinReply(
    bool Accepted,
    byte[] ActorNodeRid,
    string ActorId,
    ulong ActorGeneration,
    string ReplyContentType,
    byte[] Reply);
