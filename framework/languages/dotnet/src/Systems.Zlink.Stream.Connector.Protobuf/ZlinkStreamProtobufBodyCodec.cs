using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Options;
using Google.Protobuf;

namespace Systems.Zlink.Stream.Connector.Protobuf;

public sealed class ZlinkStreamProtobufBodyCodec : IZlinkStreamBodyCodec
{
    public ZlinkStreamCodec Codec => ZlinkStreamCodec.Protobuf;

    public bool CanSerialize(Type type)
        => typeof(IMessage).IsAssignableFrom(type);

    public ReadOnlyMemory<byte> Serialize<T>(T value)
    {
        if (value is not IMessage message)
        {
            throw Error($"{typeof(T).FullName} does not implement Google.Protobuf.IMessage.");
        }

        return message.ToByteArray();
    }

    public object? Deserialize(Type type, ReadOnlyMemory<byte> body)
    {
        if (!typeof(IMessage).IsAssignableFrom(type))
        {
            throw Error($"{type.FullName} does not implement Google.Protobuf.IMessage.");
        }

        var instance = Activator.CreateInstance(type);
        if (instance is not IMessage message)
        {
            throw Error($"{type.FullName} must have a public parameterless constructor.");
        }

        message.MergeFrom(body.Span);
        return message;
    }

    public T Deserialize<T>(ReadOnlyMemory<byte> body)
        => (T)Deserialize(typeof(T), body)!;

    private static ZlinkStreamException Error(string message)
        => new(new ZlinkStreamError(ZlinkStreamErrorCode.CodecNotFound, message));
}
