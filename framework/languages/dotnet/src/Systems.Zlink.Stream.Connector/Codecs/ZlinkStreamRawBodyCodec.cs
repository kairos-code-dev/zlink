using System.Text;

namespace Systems.Zlink.Stream.Connector.Codecs;

public sealed class ZlinkStreamRawBodyCodec : IZlinkStreamBodyCodec
{
    public ZlinkStreamCodec Codec => ZlinkStreamCodec.Raw;

    public bool CanSerialize(Type type)
        => type == typeof(string)
            || type == typeof(byte[])
            || type == typeof(ReadOnlyMemory<byte>)
            || type == typeof(Memory<byte>);

    public ReadOnlyMemory<byte> Serialize<T>(T value)
    {
        return value switch
        {
            null => ReadOnlyMemory<byte>.Empty,
            string text => Encoding.UTF8.GetBytes(text),
            byte[] bytes => bytes,
            ReadOnlyMemory<byte> memory => memory,
            Memory<byte> memory => memory,
            _ => throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.CodecNotFound,
                $"Raw codec cannot serialize {typeof(T).FullName}.")
        };
    }

    public object? Deserialize(Type type, ReadOnlyMemory<byte> body)
    {
        if (type == typeof(string))
        {
            return Encoding.UTF8.GetString(body.Span);
        }

        if (type == typeof(byte[]))
        {
            return body.ToArray();
        }

        if (type == typeof(ReadOnlyMemory<byte>))
        {
            return body;
        }

        if (type == typeof(Memory<byte>))
        {
            return body.ToArray().AsMemory();
        }

        throw ZlinkStreamConnector.Error(
            ZlinkStreamErrorCode.CodecNotFound,
            $"Raw codec cannot deserialize {type.FullName}.");
    }

    public T Deserialize<T>(ReadOnlyMemory<byte> body)
        => (T)Deserialize(typeof(T), body)!;
}
