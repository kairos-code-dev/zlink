using System.Text.Json;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionPacket(
    ZlinkStreamHeader header,
    Message payload) : IZLinkSessionPacket, IDisposable
{
    public string PacketName => header.Name;

    public ZLinkMessageMetadata Metadata { get; } = new(
        header.Metadata.Values.ToDictionary(static entry => entry.Key, static entry => entry.Value, StringComparer.Ordinal),
        new Dictionary<string, string>(StringComparer.Ordinal)
        {
            ["codec"] = header.Codec.ToString()
        });

    public ZlinkStreamHeader Header => header;

    public Message Payload => payload;

    public TMessage Decode<TMessage>()
    {
        if (header.Codec != ZlinkStreamCodec.Json)
        {
            throw new NotSupportedException(
                $"Session packet decode only supports '{ZlinkStreamCodec.Json}', not '{header.Codec}'.");
        }

        return JsonSerializer.Deserialize<TMessage>(payload.AsReadOnlySpan(), ZLinkJsonSerializerOptions.Default)
            ?? throw new InvalidOperationException($"Session packet '{PacketName}' payload is null.");
    }

    public void Dispose()
    {
        payload.Dispose();
    }
}
