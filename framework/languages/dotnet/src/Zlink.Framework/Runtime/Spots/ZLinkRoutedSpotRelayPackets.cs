using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkRoutedSpotRelayPackets
{
    public const string SendPacketName = "__zlink.routed_spot.egress.send";
    public const string RequestPacketName = "__zlink.routed_spot.egress.request";

    public static IReadOnlyList<Message> CreateRelayParts(
        ZLinkMessageKind kind,
        string targetSpotNodeChannelName,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> spotParts,
        TimeSpan? timeout = null)
    {
        var parts = new Message[spotParts.Count + 2];
        parts[0] = ZLinkEnvelopeCodec.EncodeHeader(CreateRelayHeader(
            kind,
            targetSpotNodeChannelName,
            timeout));
        var payload = CreateRelayPayloadParts(targetSpotRid, spotParts);
        for (var index = 0; index < payload.Count; index++)
        {
            parts[index + 1] = payload[index];
        }

        return parts;
    }

    public static ZLinkEnvelopeHeader CreateRelayHeader(
        ZLinkMessageKind kind,
        string targetSpotNodeChannelName,
        TimeSpan? timeout = null)
    {
        var relayPacketName = kind switch
        {
            ZLinkMessageKind.Command => SendPacketName,
            ZLinkMessageKind.Request => RequestPacketName,
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, null),
        };
        return ZLinkClientCallCodec.CreateEnvelope(
            kind,
            targetSpotNodeChannelName,
            relayPacketName,
            timeout);
    }

    public static IReadOnlyList<Message> CreateRelayPayloadParts(
        RoutingId targetSpotRid,
        IReadOnlyList<Message> spotParts)
    {
        var parts = new Message[spotParts.Count + 1];
        parts[0] = ZLinkEnvelopeCodec.EncodePart(
            new ZLinkRoutedSpotRelayMetadata(targetSpotRid.ToBytes().ToArray()));
        for (var index = 0; index < spotParts.Count; index++)
        {
            parts[index + 1] = spotParts[index];
        }

        return parts;
    }

    public static ZLinkRoutedSpotRelayMetadata DecodeMetadata(Received received)
    {
        if (received.Parts.Count < 3)
        {
            throw new InvalidOperationException("Routed SPOT relay packet requires metadata and payload parts.");
        }

        return ZLinkEnvelopeCodec.DecodePart<ZLinkRoutedSpotRelayMetadata>(received.Parts[1]);
    }

    public static IReadOnlyList<Message> CopySpotPayloadParts(Received received)
    {
        if (received.Parts.Count < 3)
        {
            throw new InvalidOperationException("Routed SPOT relay packet requires payload parts.");
        }

        var payload = new Message[received.Parts.Count - 2];
        for (var index = 2; index < received.Parts.Count; index++)
        {
            payload[index - 2] = received.Parts[index].Copy();
        }

        return payload;
    }
}

internal sealed record ZLinkRoutedSpotRelayMetadata(
    byte[] TargetSpotRid);

internal sealed record ZLinkRoutedSpotRelayReply(
    byte[][] Parts)
{
    public static ZLinkRoutedSpotRelayReply FromMessages(IReadOnlyList<Message> parts)
    {
        var payload = new byte[parts.Count][];
        for (var index = 0; index < parts.Count; index++)
        {
            payload[index] = parts[index].ToArray();
        }

        return new ZLinkRoutedSpotRelayReply(payload);
    }

    public IReadOnlyList<Message> ToMessages()
    {
        var messages = new Message[Parts.Length];
        for (var index = 0; index < Parts.Length; index++)
        {
            messages[index] = Message.From(Parts[index]);
        }

        return messages;
    }
}
