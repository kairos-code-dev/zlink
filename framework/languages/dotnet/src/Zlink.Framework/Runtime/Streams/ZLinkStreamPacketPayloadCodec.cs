using System.Text;
using System.Text.Json;
using Google.Protobuf;
using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;

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

        if (header.Codec == ZlinkStreamCodec.MessagePack)
        {
            return MessagePackSerializer.Deserialize(messageType, payload, MessagePackSerializerOptions.Standard);
        }

        if (header.Codec == ZlinkStreamCodec.Protobuf)
        {
            if (!typeof(IMessage).IsAssignableFrom(messageType))
            {
                throw new InvalidOperationException(
                    $"Protobuf actor packet '{header.Name}' cannot be decoded as '{messageType}'.");
            }

            var message = (IMessage?)Activator.CreateInstance(messageType)
                ?? throw new InvalidOperationException($"{messageType.FullName} must have a public parameterless constructor.");
            message.MergeFrom(payload.Span);
            return message;
        }

        throw new InvalidOperationException(
            $"Actor packet '{header.Name}' uses codec '{header.Codec}'. Register a ZlinkStreamEncodedPayload handler and decode it explicitly.");
    }

    public static ZlinkStreamEncodedPayload Encode(object? message, Type messageType)
    {
        if (message is IMessage protobuf)
        {
            return new ZlinkStreamEncodedPayload(
                ZlinkStreamCodec.Protobuf,
                protobuf.ToByteArray(),
                messageType);
        }

        if (message is not null
            && messageType.GetCustomAttributes(typeof(MessagePackObjectAttribute), inherit: true).Length > 0)
        {
            return new ZlinkStreamEncodedPayload(
                ZlinkStreamCodec.MessagePack,
                MessagePackSerializer.Serialize(messageType, message, MessagePackSerializerOptions.Standard),
                messageType);
        }

        return new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.Json,
            EncodeJson(message, messageType),
            messageType);
    }

    public static byte[] EncodeJson(object? message, Type messageType)
        => ZLinkEnvelopeCodec.EncodeJsonBytes(message, messageType);
}
