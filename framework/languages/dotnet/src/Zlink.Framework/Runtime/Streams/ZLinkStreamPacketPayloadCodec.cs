using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamPacketPayloadCodec
{
    public static object? Decode(
        ZlinkStreamHeader header,
        Message payloadMessage,
        Type messageType)
    {
        if (messageType == typeof(Message))
        {
            return payloadMessage;
        }

        var payload = payloadMessage.AsReadOnlyMemory();
        if ((header.Flags & ZlinkStreamHeaderFlags.PayloadCompressed) != 0)
        {
            payload = ZLinkStreamProtocolDefaults.Lz4Decompress(payload);
        }

        if (messageType == typeof(ZlinkStreamEncodedPayload))
        {
            return new ZlinkStreamEncodedPayload(header.Codec, payload);
        }

        if (messageType == typeof(ReadOnlyMemory<byte>))
        {
            return payload;
        }

        if (header.Codec == ZlinkStreamCodec.Raw)
        {
            if (messageType == typeof(string))
            {
                return Encoding.UTF8.GetString(payload.Span);
            }

            if (messageType == typeof(byte[]))
            {
                return payload.ToArray();
            }

            throw new InvalidOperationException(
                $"Raw actor packet '{header.Name}' cannot be decoded as '{messageType}'.");
        }

        if (header.Codec == ZlinkStreamCodec.Json)
        {
            return JsonSerializer.Deserialize(payload.Span, messageType, ZLinkJsonSerializerOptions.Default);
        }

        throw new InvalidOperationException(
            $"Actor packet '{header.Name}' uses codec '{header.Codec}'. Register a ZlinkStreamEncodedPayload handler and decode it explicitly.");
    }

    public static byte[] EncodeJson(object? message, Type messageType)
        => ZLinkEnvelopeCodec.EncodeJsonBytes(message, messageType);
}
