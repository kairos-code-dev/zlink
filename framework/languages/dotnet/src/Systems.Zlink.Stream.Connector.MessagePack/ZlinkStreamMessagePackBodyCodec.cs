using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Options;
using MessagePack;

namespace Systems.Zlink.Stream.Connector.MessagePack;

public sealed class ZlinkStreamMessagePackBodyCodec : IZlinkStreamBodyCodec
{
    private readonly MessagePackSerializerOptions? _options;

    public ZlinkStreamMessagePackBodyCodec(MessagePackSerializerOptions? options = null)
    {
        _options = options;
    }

    public ZlinkStreamCodec Codec => ZlinkStreamCodec.MessagePack;

    public bool CanSerialize(Type type)
        => type.GetCustomAttributes(typeof(MessagePackObjectAttribute), true).Length > 0;

    public ReadOnlyMemory<byte> Serialize<T>(T value)
        => MessagePackSerializer.Serialize(value, _options);

    public object? Deserialize(Type type, ReadOnlyMemory<byte> body)
        => MessagePackSerializer.Deserialize(type, body, _options);

    public T Deserialize<T>(ReadOnlyMemory<byte> body)
        => MessagePackSerializer.Deserialize<T>(body, _options);
}
