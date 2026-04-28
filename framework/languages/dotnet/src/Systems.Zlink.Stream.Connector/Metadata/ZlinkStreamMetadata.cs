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

namespace Systems.Zlink.Stream.Connector.Metadata;

public sealed class ZlinkStreamMetadata
{
    public static ZlinkStreamMetadata Empty { get; } = new(new Dictionary<string, string>());

    private ZlinkStreamMetadata(IReadOnlyDictionary<string, string> values)
    {
        Values = values;
    }

    public int Count => Values.Count;

    public IReadOnlyDictionary<string, string> Values { get; }

    public string? Get(string key) => Values.TryGetValue(key, out var value) ? value : null;

    public bool TryGet(string key, out string value) => Values.TryGetValue(key, out value!);

    public ZlinkStreamMetadata With(string key, string value)
    {
        ValidateKey(key);
        ArgumentNullException.ThrowIfNull(value);

        var copy = new Dictionary<string, string>(Values, StringComparer.Ordinal)
        {
            [key] = value
        };
        return new ZlinkStreamMetadata(copy);
    }

    public ZlinkStreamMetadata WithMany(IEnumerable<KeyValuePair<string, string>> values)
    {
        ArgumentNullException.ThrowIfNull(values);

        var copy = new Dictionary<string, string>(Values, StringComparer.Ordinal);
        foreach (var (key, value) in values)
        {
            ValidateKey(key);
            ArgumentNullException.ThrowIfNull(value);
            copy[key] = value;
        }

        return new ZlinkStreamMetadata(copy);
    }

    internal static ZlinkStreamMetadata FromDictionary(Dictionary<string, string> values)
        => values.Count == 0 ? Empty : new ZlinkStreamMetadata(values);

    private static void ValidateKey(string key)
    {
        if (string.IsNullOrEmpty(key))
        {
            throw new ZlinkStreamException(new ZlinkStreamError(
                ZlinkStreamErrorCode.ValidationFailed,
                "Metadata key must not be empty."));
        }
    }
}
