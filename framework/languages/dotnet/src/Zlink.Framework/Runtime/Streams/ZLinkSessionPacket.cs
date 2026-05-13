using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionPacket(
    ZlinkStreamHeader header,
    Message body) : IZLinkSessionPacket, IDisposable
{
    public string PacketName => header.Name;

    public ZLinkMessageMetadata Metadata { get; } = new(
        header.Metadata.Values.ToDictionary(static entry => entry.Key, static entry => entry.Value, StringComparer.Ordinal),
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["codec"] = header.Codec.ToString()
        });

    public ZlinkStreamHeader Header => header;

    public Message Body => body;

    public TMessage Decode<TMessage>()
    {
        if (header.Codec != ZlinkStreamCodec.Json)
        {
            throw new NotSupportedException(
                $"Session packet decode only supports '{ZlinkStreamCodec.Json}', not '{header.Codec}'.");
        }

        return JsonSerializer.Deserialize<TMessage>(body.AsReadOnlySpan(), ZLinkJsonSerializerOptions.Default)
            ?? throw new InvalidOperationException($"Session packet '{PacketName}' body is null.");
    }

    public void Dispose()
    {
        body.Dispose();
    }
}
