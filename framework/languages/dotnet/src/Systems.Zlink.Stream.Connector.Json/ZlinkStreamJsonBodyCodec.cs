using Systems.Zlink.Stream.Connector.Abstractions;
using Systems.Zlink.Stream.Connector.Connector;
using Systems.Zlink.Stream.Connector.Options;
using System.Text.Json;

namespace Systems.Zlink.Stream.Connector.Json;

public sealed class ZlinkStreamJsonBodyCodec : IZlinkStreamBodyCodec
{
    private readonly JsonSerializerOptions _options;

    public ZlinkStreamJsonBodyCodec(JsonSerializerOptions? options = null)
    {
        _options = options ?? new JsonSerializerOptions(JsonSerializerDefaults.Web);
    }

    public ZlinkStreamCodec Codec => ZlinkStreamCodec.Json;

    public bool CanSerialize(Type type) => true;

    public ReadOnlyMemory<byte> Serialize<T>(T value)
        => JsonSerializer.SerializeToUtf8Bytes(value, _options);

    public object? Deserialize(Type type, ReadOnlyMemory<byte> body)
        => JsonSerializer.Deserialize(body.Span, type, _options);

    public T Deserialize<T>(ReadOnlyMemory<byte> body)
        => JsonSerializer.Deserialize<T>(body.Span, _options)!;
}
