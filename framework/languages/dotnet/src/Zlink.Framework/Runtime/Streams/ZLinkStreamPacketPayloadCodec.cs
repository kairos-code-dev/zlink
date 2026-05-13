using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Protocol.Compression;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamPacketPayloadCodec
{
    private static readonly ZlinkStreamLz4CompressionCodec CompressionCodec = new();

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
        => JsonSerializer.SerializeToUtf8Bytes(message, messageType, ZLinkJsonSerializerOptions.Default);
}
