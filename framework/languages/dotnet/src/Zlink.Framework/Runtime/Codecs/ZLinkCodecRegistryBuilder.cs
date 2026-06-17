namespace Zlink.Framework.Runtime.Codecs;

internal sealed class ZLinkCodecRegistryBuilder : IZLinkCodecRegistryBuilder
{
    private readonly HashSet<string> _codecs = new(StringComparer.Ordinal);
    private readonly Dictionary<string, IZLinkMessageSerializer> _serializers =
        new(StringComparer.OrdinalIgnoreCase);

    public IReadOnlyCollection<string> RegisteredCodecs => _codecs;

    public IReadOnlyDictionary<string, IZLinkMessageSerializer> Serializers => _serializers;

    public void AddProtobuf()
    {
        _codecs.Add("protobuf");
    }

    public void AddJson()
    {
        _codecs.Add("json");
    }

    public void AddMessagePack()
    {
        _codecs.Add("messagepack");
    }

    public void AddSerializer(string contentType, IZLinkMessageSerializer serializer)
    {
        ArgumentNullException.ThrowIfNull(serializer);
        if (string.IsNullOrWhiteSpace(contentType))
        {
            throw new ArgumentException("Custom serializer content type must not be blank.", nameof(contentType));
        }

        _serializers[contentType.Trim()] = serializer;
    }

    /// <summary>
    /// The single registered custom serializer with its content type, or <c>null</c>
    /// when none is registered. Throws when more than one is registered because the
    /// outgoing payload serializer would then be ambiguous.
    /// </summary>
    public (string ContentType, IZLinkMessageSerializer Serializer)? SingleCustomSerializer()
    {
        if (_serializers.Count == 0)
        {
            return null;
        }

        if (_serializers.Count > 1)
        {
            throw new InvalidOperationException(
                "Payload serializer is ambiguous because more than one custom serializer is registered: "
                    + string.Join(", ", _serializers.Keys));
        }

        var entry = _serializers.First();
        return (entry.Key, entry.Value);
    }

    public bool TryGetSerializer(string contentType, out IZLinkMessageSerializer serializer)
    {
        if (!string.IsNullOrEmpty(contentType) && _serializers.TryGetValue(contentType, out var found))
        {
            serializer = found;
            return true;
        }

        serializer = null!;
        return false;
    }
}
