namespace Zlink.Framework.Runtime.Codecs;

internal sealed class ZLinkCodecRegistryBuilder : IZLinkCodecRegistryBuilder
{
    private readonly HashSet<string> _codecs = new(StringComparer.Ordinal);

    private readonly Dictionary<ZlinkStreamCodec, string> _contentTypesByStreamCodec =
        [];

    private readonly Dictionary<string, RegisteredSerializer> _serializers =
        new(StringComparer.OrdinalIgnoreCase);

    private readonly Dictionary<string, ZlinkStreamCodec> _streamCodecsByContentType =
        new(StringComparer.OrdinalIgnoreCase);

    public IReadOnlyCollection<string> RegisteredCodecs => _codecs;

    public IReadOnlyDictionary<string, IZLinkMessageSerializer> Serializers =>
        _serializers.ToDictionary(entry => entry.Key, entry => entry.Value.Serializer,
            StringComparer.OrdinalIgnoreCase);

    public void Use(IZLinkCodecExtension extension)
    {
        ArgumentNullException.ThrowIfNull(extension);
        extension.Register(this);
    }

    public void AddJson()
    {
        _codecs.Add("json");
    }

    public void AddSerializer(string contentType, IZLinkMessageSerializer serializer)
    {
        AddSerializer(contentType, serializer, _ => true, true);
    }

    public void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize)
    {
        AddSerializer(contentType, serializer, canSerialize, false);
    }

    public void AddStreamCodec(
        string contentType,
        ZlinkStreamCodec codec)
    {
        if (string.IsNullOrWhiteSpace(contentType))
            throw new ArgumentException("Stream codec content type must not be blank.", nameof(contentType));

        var normalized = contentType.Trim();
        _streamCodecsByContentType[normalized] = codec;
        _contentTypesByStreamCodec[codec] = normalized;
    }

    private void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize,
        bool isFallbackSerializer)
    {
        ArgumentNullException.ThrowIfNull(serializer);
        ArgumentNullException.ThrowIfNull(canSerialize);
        if (string.IsNullOrWhiteSpace(contentType))
            throw new ArgumentException("Custom serializer content type must not be blank.", nameof(contentType));

        _serializers[contentType.Trim()] = new RegisteredSerializer(serializer, canSerialize, isFallbackSerializer);
    }

    /// <summary>
    ///     The single registered custom serializer with its content type, or <c>null</c>
    ///     when none is registered. Throws when more than one is registered because the
    ///     outgoing payload serializer would then be ambiguous.
    /// </summary>
    public (string ContentType, IZLinkMessageSerializer Serializer)? SingleCustomSerializer()
    {
        var fallbackSerializers = _serializers
            .Where(entry => entry.Value.IsFallbackSerializer)
            .ToArray();

        if (fallbackSerializers.Length == 0) return null;

        if (fallbackSerializers.Length > 1)
            throw new InvalidOperationException(
                "Payload serializer is ambiguous because more than one custom serializer is registered: "
                + string.Join(", ", fallbackSerializers.Select(entry => entry.Key)));

        var entry = fallbackSerializers[0];
        return (entry.Key, entry.Value.Serializer);
    }

    public bool TryGetSerializer(string contentType, out IZLinkMessageSerializer serializer)
    {
        if (!string.IsNullOrEmpty(contentType) && _serializers.TryGetValue(contentType, out var found))
        {
            serializer = found.Serializer;
            return true;
        }

        serializer = null!;
        return false;
    }

    public bool TryResolveSerializer(Type payloadType, out string contentType, out IZLinkMessageSerializer serializer)
    {
        var matches = _serializers
            .Where(entry => entry.Value.CanSerialize(payloadType))
            .ToArray();

        if (matches.Length == 0)
        {
            contentType = string.Empty;
            serializer = null!;
            return false;
        }

        if (matches.Length > 1)
            throw new InvalidOperationException(
                "Payload serializer is ambiguous for type '" + payloadType + "': "
                + string.Join(", ", matches.Select(entry => entry.Key)));

        contentType = matches[0].Key;
        serializer = matches[0].Value.Serializer;
        return true;
    }

    public bool TryResolveStreamCodec(string contentType, out ZlinkStreamCodec codec)
    {
        return _streamCodecsByContentType.TryGetValue(contentType, out codec);
    }

    public bool TryResolveStreamContentType(
        ZlinkStreamCodec codec,
        out string contentType)
    {
        return _contentTypesByStreamCodec.TryGetValue(codec, out contentType!);
    }

    private sealed record RegisteredSerializer(
        IZLinkMessageSerializer Serializer,
        Func<Type, bool> CanSerialize,
        bool IsFallbackSerializer);
}