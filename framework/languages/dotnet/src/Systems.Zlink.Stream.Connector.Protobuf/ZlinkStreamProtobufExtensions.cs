using Google.Protobuf;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Systems.Zlink.Stream.Connector.Protobuf;

public static class ZlinkStreamProtobufExtensions
{
    public static T FromProto<T>(this ZlinkStreamEncodedPayload payload)
        where T : IMessage<T>, new()
    {
        EnsureProtobuf(payload);

        T value = new();
        value.MergeFrom(payload.Payload.Span);
        return value;
    }

    public static ZlinkStreamEncodedPayload ToProto<T>(this T value)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(value);
        return new ZlinkStreamEncodedPayload(
            ZlinkStreamCodec.Protobuf,
            value.ToByteArray(),
            typeof(T));
    }

    private static void EnsureProtobuf(ZlinkStreamEncodedPayload payload)
    {
        if (payload.Codec != ZlinkStreamCodec.Protobuf)
        {
            throw new InvalidOperationException($"Stream payload codec is {payload.Codec}, not Protobuf.");
        }
    }
}
