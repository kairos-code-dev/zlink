using System.Text.Json;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs;

namespace Zlink.HttpClient.Runtime;

internal sealed class HttpClientCodecRegistry : IZLinkCodecRegistryBuilder
{
    private const string JsonContentType = "application/json";
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    private readonly Dictionary<string, RegisteredSerializer> _serializers =
        new(StringComparer.OrdinalIgnoreCase);

    public HttpClientCodecRegistry()
    {
    }

    private HttpClientCodecRegistry(HttpClientCodecRegistry source)
    {
        foreach (var (contentType, serializer) in source._serializers)
        {
            _serializers[contentType] = serializer;
        }
    }

    public HttpClientCodecRegistry Snapshot() => new(this);

    public void Use(IZLinkCodecExtension extension)
    {
        ArgumentNullException.ThrowIfNull(extension);
        extension.Register(this);
    }

    public void AddJson()
    {
    }

    public void AddSerializer(string contentType, IZLinkMessageSerializer serializer)
        => AddSerializer(contentType, serializer, _ => true, isFallbackSerializer: true);

    public void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize)
        => AddSerializer(contentType, serializer, canSerialize, isFallbackSerializer: false);

    public void AddStreamCodec(
        string contentType,
        Systems.Zlink.Stream.Connector.Contracts.ZlinkStreamCodec codec)
    {
    }

    public (byte[] Body, string ContentType) Encode(object? value, Type type)
    {
        if (value is null)
        {
            return ([], JsonContentType);
        }

        if (TryResolveSerializer(type, out var contentType, out var serializer)
            || TryResolveFallback(out contentType, out serializer))
        {
            var payload = serializer.Serialize(value, type);
            return (payload.ToArray(), contentType);
        }

        return (
            JsonSerializer.SerializeToUtf8Bytes(value, type, JsonOptions),
            JsonContentType);
    }

    public object? Decode(byte[] body, Type type, string? contentType)
    {
        if (type == typeof(byte[]))
        {
            return body;
        }

        if (type == typeof(ReadOnlyMemory<byte>))
        {
            return new ReadOnlyMemory<byte>(body);
        }

        if (body.Length == 0)
        {
            return type.IsValueType ? Activator.CreateInstance(type) : null;
        }

        if (!string.IsNullOrWhiteSpace(contentType)
            && _serializers.TryGetValue(NormalizeContentType(contentType), out var found))
        {
            return found.Serializer.Deserialize(ZLinkEncodedPayload.From(body), type);
        }

        return JsonSerializer.Deserialize(body, type, JsonOptions);
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
        {
            throw new ArgumentException("HTTP codec content type must not be blank.", nameof(contentType));
        }

        _serializers[NormalizeContentType(contentType)] =
            new RegisteredSerializer(serializer, canSerialize, isFallbackSerializer);
    }

    private bool TryResolveSerializer(
        Type payloadType,
        out string contentType,
        out IZLinkMessageSerializer serializer)
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
        {
            throw new InvalidOperationException(
                "HTTP payload serializer is ambiguous for type '" + payloadType + "': "
                    + string.Join(", ", matches.Select(entry => entry.Key)));
        }

        contentType = matches[0].Key;
        serializer = matches[0].Value.Serializer;
        return true;
    }

    private bool TryResolveFallback(out string contentType, out IZLinkMessageSerializer serializer)
    {
        var fallbackSerializers = _serializers
            .Where(entry => entry.Value.IsFallbackSerializer)
            .ToArray();

        if (fallbackSerializers.Length == 0)
        {
            contentType = string.Empty;
            serializer = null!;
            return false;
        }

        if (fallbackSerializers.Length > 1)
        {
            throw new InvalidOperationException(
                "HTTP payload serializer is ambiguous because more than one custom serializer is registered: "
                    + string.Join(", ", fallbackSerializers.Select(entry => entry.Key)));
        }

        contentType = fallbackSerializers[0].Key;
        serializer = fallbackSerializers[0].Value.Serializer;
        return true;
    }

    private static string NormalizeContentType(string contentType)
    {
        var separator = contentType.IndexOf(';');
        return (separator < 0 ? contentType : contentType[..separator]).Trim();
    }

    private sealed record RegisteredSerializer(
        IZLinkMessageSerializer Serializer,
        Func<Type, bool> CanSerialize,
        bool IsFallbackSerializer);
}
