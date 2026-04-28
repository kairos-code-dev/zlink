using System.Buffers.Binary;
using System.Collections.Concurrent;

using System.Net.Security;

using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Codecs;

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
