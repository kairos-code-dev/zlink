using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamPacketPayloadCodec
{
    private static readonly IZlinkStreamCompressionCodec CompressionCodec = ZlinkStreamDefaultCodecs.Lz4Compression();

    public static object? Decode(
        ZlinkStreamHeader header,
        Message body,
        Type messageType)
    {
        if (messageType == typeof(Message))
        {
            return body;
        }

        var payload = body.AsReadOnlyMemory();
        if ((header.Flags & ZlinkStreamHeaderFlags.BodyCompressed) != 0)
        {
            payload = CompressionCodec.Decompress(payload);
        }

        if (messageType == typeof(ZlinkStreamEncodedBody))
        {
            return new ZlinkStreamEncodedBody(header.Codec, payload);
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
            $"Actor packet '{header.Name}' uses codec '{header.Codec}'. Register a ZlinkStreamEncodedBody handler and decode it explicitly.");
    }

    public static byte[] EncodeJson(object? message, Type messageType)
        => ZLinkEnvelopeCodec.EncodeJsonBytes(message, messageType);
}
