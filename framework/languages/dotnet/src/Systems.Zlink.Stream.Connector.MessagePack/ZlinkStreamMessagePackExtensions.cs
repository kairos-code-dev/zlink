using MessagePack;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Systems.Zlink.Stream.Connector.MessagePack;

public static class ZlinkStreamMessagePackCodec
{
    private static MessagePackSerializerOptions _serializerOptions = MessagePackSerializerOptions.Standard;

    public static MessagePackSerializerOptions SerializerOptions => _serializerOptions;

    public static void Configure(MessagePackSerializerOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _serializerOptions = options;
    }
}

public static class ZlinkStreamMessagePackExtensions
{
    public static T FromMsgPack<T>(this ZlinkStreamEncodedPayload payload)
    {
        EnsureMessagePack(payload);
        return MessagePackSerializer.Deserialize<T>(payload.Payload, ZlinkStreamMessagePackCodec.SerializerOptions);
    }

    public static ZlinkStreamEncodedPayload ToMsgPack<T>(this T value)
    {
        return new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.MessagePack,
            MessagePackSerializer.Serialize(value, ZlinkStreamMessagePackCodec.SerializerOptions),
            typeof(T));
    }

    private static void EnsureMessagePack(ZlinkStreamEncodedPayload payload)
    {
        if (payload.Codec != ZlinkStreamCodec.MessagePack)
        {
            throw new InvalidOperationException($"Stream payload codec is {payload.Codec}, not MessagePack.");
        }
    }
}
