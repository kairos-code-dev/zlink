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
    public static T FromMsgPack<T>(this ZlinkStreamEncodedBody body)
    {
        EnsureMessagePack(body);
        return MessagePackSerializer.Deserialize<T>(body.Body, ZlinkStreamMessagePackCodec.SerializerOptions);
    }

    public static ZlinkStreamEncodedBody ToMsgPack<T>(this T value)
    {
        return new ZlinkStreamEncodedBody(
            ZlinkStreamCodec.MessagePack,
            MessagePackSerializer.Serialize(value, ZlinkStreamMessagePackCodec.SerializerOptions),
            typeof(T));
    }

    private static void EnsureMessagePack(ZlinkStreamEncodedBody body)
    {
        if (body.Codec != ZlinkStreamCodec.MessagePack)
        {
            throw new InvalidOperationException($"Stream body codec is {body.Codec}, not MessagePack.");
        }
    }
}
