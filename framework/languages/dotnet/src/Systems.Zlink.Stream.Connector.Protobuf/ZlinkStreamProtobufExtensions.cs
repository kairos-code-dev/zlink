using Google.Protobuf;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Systems.Zlink.Stream.Connector.Protobuf;

public static class ZlinkStreamProtobufExtensions
{
    public static T FromProto<T>(this ZlinkStreamEncodedBody body)
        where T : IMessage<T>, new()
    {
        EnsureProtobuf(body);

        T value = new();
        value.MergeFrom(body.Body.Span);
        return value;
    }

    public static ZlinkStreamEncodedBody ToProto<T>(this T value)
        where T : IMessage<T>
    {
        ArgumentNullException.ThrowIfNull(value);
        return new ZlinkStreamEncodedBody(
            ZlinkStreamCodec.Protobuf,
            value.ToByteArray(),
            typeof(T));
    }

    private static void EnsureProtobuf(ZlinkStreamEncodedBody body)
    {
        if (body.Codec != ZlinkStreamCodec.Protobuf)
        {
            throw new InvalidOperationException($"Stream body codec is {body.Codec}, not Protobuf.");
        }
    }
}
