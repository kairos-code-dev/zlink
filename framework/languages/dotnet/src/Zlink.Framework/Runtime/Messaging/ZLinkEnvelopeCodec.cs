using System.Collections.Concurrent;
using System.Text.Json;
using System.Threading;

namespace Zlink.Framework.Runtime.Messaging;

internal enum ZLinkMessageKind
{
    Request = 1,
    Response = 2,
    Command = 3,
    Publish = 4,
    Error = 5
}

internal sealed record ZLinkEnvelopeHeader(
    ZLinkMessageKind Kind,
    string ChannelName,
    string MessageName,
    string ContentType,
    string? CorrelationId,
    DateTimeOffset? Deadline,
    string? Topic,
    string? ErrorCode,
    string? ErrorMessage,
    string? Source = null);

internal static class ZLinkEnvelopeCodec
{
    private const string JsonContentType = "application/json";
    private static readonly ConcurrentDictionary<SimpleHeaderKey, byte[]> SimpleHeaderCache = new();
    private static readonly object DecodedHeaderCacheGate = new();
    private static HeaderCacheEntry[] DecodedHeaderCache = [];

    public static string DefaultContentType => JsonContentType;

    public static IReadOnlyList<Message> EncodeParts(
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        return EncodeParts(header, body, bodyType, null);
    }

    public static IReadOnlyList<Message> EncodeParts(
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType,
        ZLinkCodecRegistryBuilder? codecs)
    {
        if (TryResolveBodySerializer(body, bodyType, codecs, out var contentType, out var serializer))
        {
            var headerMessage = EncodeHeader(header with { ContentType = contentType });
            try
            {
                return ZLinkMessageParts.Create(
                    headerMessage,
                    EncodeBodyWithSerializer(body!, bodyType!, serializer!));
            }
            catch
            {
                headerMessage.Dispose();
                throw;
            }
        }

        return ZLinkMessageParts.Create(
            EncodeHeader(header with { ContentType = ResolveContentType(body, bodyType, codecs) }),
            EncodeBody(body, bodyType, codecs));
    }

    public static IReadOnlyList<Message> EncodeRawBodyParts(
        ZLinkEnvelopeHeader header,
        Message body)
    {
        return ZLinkMessageParts.Create(EncodeHeader(header), body);
    }

    public static Message EncodeHeader(ZLinkEnvelopeHeader header)
    {
        if (IsSimpleHeader(header))
        {
            var bytes = SimpleHeaderCache.GetOrAdd(
                new SimpleHeaderKey(header.Kind, header.ChannelName, header.MessageName, header.ContentType),
                static key => EncodeJsonBytes(new ZLinkEnvelopeHeader(
                    key.Kind,
                    key.ChannelName,
                    key.MessageName,
                    key.ContentType,
                    null,
                    null,
                    null,
                    null,
                    null)));
            return Message.From(bytes);
        }

        return EncodeJsonPart(header);
    }

    public static Message EncodeBody(object? body, Type? bodyType)
    {
        return EncodeBody(body, bodyType, null);
    }

    public static Message EncodeBody(object? body, Type? bodyType, ZLinkCodecRegistryBuilder? codecs)
    {
        if (bodyType is null || body is null) return Message.From(ReadOnlySpan<byte>.Empty);

        if (bodyType == typeof(Message))
        {
            if (body is not Message message)
                throw new InvalidOperationException(
                    $"Envelope body type is Message, but body instance is '{body.GetType()}'.");

            return Message.From(message);
        }

        if (bodyType == typeof(ZLinkMessage))
        {
            if (body is not ZLinkMessage message)
                throw new InvalidOperationException(
                    $"Envelope body type is ZLinkMessage, but body instance is '{body.GetType()}'.");

            return Message.From(message.Encode(codecs ?? new ZLinkCodecRegistryBuilder()).Payload.Bytes.Span);
        }

        if (codecs is not null
            && codecs.TryResolveSerializer(bodyType, out _, out var serializer))
        {
            if (serializer is IZLinkMessagePartSerializer partSerializer)
                return partSerializer.SerializePart(body, bodyType);

            return Message.From(serializer.Serialize(body, bodyType).Bytes.Span);
        }

        if (codecs?.SingleCustomSerializer() is { } custom)
        {
            if (custom.Serializer is IZLinkMessagePartSerializer partSerializer)
                return partSerializer.SerializePart(body, bodyType);

            return Message.From(custom.Serializer.Serialize(body, bodyType).Bytes.Span);
        }

        return EncodeJsonPart(body, bodyType);
    }

    private static Message EncodeBodyWithSerializer(
        object body,
        Type bodyType,
        IZLinkMessageSerializer serializer)
    {
        if (serializer is IZLinkMessagePartSerializer partSerializer)
            return partSerializer.SerializePart(body, bodyType);

        return Message.From(serializer.Serialize(body, bodyType).Bytes.Span);
    }

    public static T DecodePart<T>(Message message)
    {
        return JsonSerializer.Deserialize<T>(message.AsReadOnlySpan(), ZLinkJsonSerializerOptions.Default)
               ?? throw new InvalidOperationException($"Invalid {typeof(T).Name} message part.");
    }

    public static Message EncodePart<T>(T value)
    {
        return EncodeJsonPart(value);
    }

    public static ZLinkEnvelopeHeader DecodeHeader(Message message)
    {
        var bytes = message.AsReadOnlySpan();
        var hash = HashBytes(bytes);
        var cache = Volatile.Read(ref DecodedHeaderCache);
        foreach (var entry in cache)
        {
            if (entry.Hash == hash && entry.Bytes.AsSpan().SequenceEqual(bytes))
                return entry.Header;
        }

        var header = JsonSerializer.Deserialize<ZLinkEnvelopeHeader>(
                         bytes,
                         ZLinkJsonSerializerOptions.Default)
                     ?? throw new InvalidOperationException("Invalid ZLink envelope header.");
        AddDecodedHeaderCacheEntry(bytes, hash, header);
        return header;
    }

    public static ZLinkEnvelopeHeader DecodeHeader(IReadOnlyList<Message> parts)
    {
        EnsurePart(parts, 0, "header");
        return DecodeHeader(parts[0]);
    }

    public static object? DecodeBody(IReadOnlyList<Message> parts, Type bodyType)
    {
        return DecodeBody(parts, bodyType, null);
    }

    public static object? DecodeBody(
        IReadOnlyList<Message> parts,
        Type bodyType,
        ZLinkCodecRegistryBuilder? codecs)
    {
        EnsurePart(parts, 0, "header");
        return DecodeBody(parts, bodyType, DecodeHeader(parts[0]).ContentType, codecs);
    }

    public static object? DecodeBody(
        IReadOnlyList<Message> parts,
        Type bodyType,
        string contentType,
        ZLinkCodecRegistryBuilder? codecs)
    {
        EnsurePart(parts, 1, "body");
        return DecodeBody(parts[1], bodyType, contentType, codecs);
    }

    public static object? DecodeBody(Message bodyMessage, Type bodyType)
    {
        return DecodeBody(bodyMessage, bodyType, JsonContentType, null);
    }

    public static object? DecodeBody(Message bodyMessage, Type bodyType, string contentType)
    {
        return DecodeBody(bodyMessage, bodyType, contentType, null);
    }

    public static object? DecodeBody(
        Message bodyMessage,
        Type bodyType,
        string contentType,
        ZLinkCodecRegistryBuilder? codecs)
    {
        if (bodyType == typeof(Message)) return bodyMessage;

        if (bodyType == typeof(ZLinkMessage))
            return ZLinkMessage.FromEnvelopePayload(contentType, bodyMessage,
                codecs ?? new ZLinkCodecRegistryBuilder());

        if (bodyType == typeof(ReadOnlyMemory<byte>)) return bodyMessage.AsReadOnlyMemory();

        if (bodyMessage.Size == 0)
            return bodyType.IsValueType
                ? Activator.CreateInstance(bodyType)
                : null;

        if (codecs is not null
            && codecs.TryGetSerializer(contentType, out var customSerializer))
        {
            if (customSerializer is IZLinkMessageSpanDeserializer spanDeserializer)
                return spanDeserializer.Deserialize(bodyMessage.AsReadOnlySpan(), bodyType);

            return customSerializer.Deserialize(
                ZLinkEncodedPayload.From(bodyMessage.AsReadOnlyMemory()),
                bodyType);
        }

        return JsonSerializer.Deserialize(
            bodyMessage.AsReadOnlySpan(),
            bodyType,
            ZLinkJsonSerializerOptions.Default);
    }

    public static byte[] DecodeBytes(IReadOnlyList<Message> parts)
    {
        EnsurePart(parts, 1, "body");
        return parts[1].ToArray();
    }

    public static Message EncodeJsonPart<T>(T value)
    {
        return Message.From(EncodeJsonBytes(value));
    }

    public static Message EncodeJsonPart(object? value, Type valueType)
    {
        return Message.From(EncodeJsonBytes(value, valueType));
    }

    public static byte[] EncodeJsonBytes<T>(T value)
    {
        return JsonSerializer.SerializeToUtf8Bytes(value, ZLinkJsonSerializerOptions.Default);
    }

    public static byte[] EncodeJsonBytes(object? value, Type valueType)
    {
        return JsonSerializer.SerializeToUtf8Bytes(value, valueType, ZLinkJsonSerializerOptions.Default);
    }

    private static string ResolveContentType(object? body, Type? bodyType, ZLinkCodecRegistryBuilder? codecs)
    {
        if (body is null || bodyType is null) return JsonContentType;

        if (bodyType == typeof(Message) || body is Message) return JsonContentType;

        if (bodyType == typeof(ZLinkMessage) && body is ZLinkMessage message)
        {
            var encoded = message.Encode(codecs ?? new ZLinkCodecRegistryBuilder());
            return encoded.ContentType;
        }

        if (codecs is not null
            && codecs.TryResolveSerializer(bodyType, out var contentType, out _))
            return contentType;

        if (codecs?.SingleCustomSerializer() is { } custom) return custom.ContentType;

        return JsonContentType;
    }

    private static bool TryResolveBodySerializer(
        object? body,
        Type? bodyType,
        ZLinkCodecRegistryBuilder? codecs,
        out string contentType,
        out IZLinkMessageSerializer? serializer)
    {
        contentType = JsonContentType;
        serializer = null;
        if (body is null || bodyType is null) return false;
        if (bodyType == typeof(Message) || body is Message) return false;
        if (bodyType == typeof(ZLinkMessage) || body is ZLinkMessage) return false;

        if (codecs is not null
            && codecs.TryResolveSerializer(bodyType, out contentType, out serializer))
            return true;

        if (codecs?.SingleCustomSerializer() is { } custom)
        {
            contentType = custom.ContentType;
            serializer = custom.Serializer;
            return true;
        }

        return false;
    }

    private static void EnsurePart(IReadOnlyList<Message> parts, int index, string name)
    {
        if (parts.Count <= index) throw new InvalidOperationException($"ZLink envelope {name} part is missing.");
    }

    private static bool IsSimpleHeader(ZLinkEnvelopeHeader header)
    {
        return header.CorrelationId is null
               && header.Deadline is null
               && header.Topic is null
               && header.ErrorCode is null
               && header.ErrorMessage is null
               && header.Source is null;
    }

    private readonly record struct SimpleHeaderKey(
        ZLinkMessageKind Kind,
        string ChannelName,
        string MessageName,
        string ContentType);

    private static void AddDecodedHeaderCacheEntry(
        ReadOnlySpan<byte> bytes,
        ulong hash,
        ZLinkEnvelopeHeader header)
    {
        if (bytes.Length > 1024) return;

        lock (DecodedHeaderCacheGate)
        {
            var cache = DecodedHeaderCache;
            foreach (var entry in cache)
            {
                if (entry.Hash == hash && entry.Bytes.AsSpan().SequenceEqual(bytes))
                    return;
            }

            var copy = bytes.ToArray();
            var next = cache.Length < 64
                ? new HeaderCacheEntry[cache.Length + 1]
                : new HeaderCacheEntry[cache.Length];
            if (cache.Length == next.Length)
            {
                Array.Copy(cache, 1, next, 0, next.Length - 1);
                next[^1] = new HeaderCacheEntry(copy, hash, header);
            }
            else
            {
                Array.Copy(cache, next, cache.Length);
                next[^1] = new HeaderCacheEntry(copy, hash, header);
            }

            Volatile.Write(ref DecodedHeaderCache, next);
        }
    }

    private static ulong HashBytes(ReadOnlySpan<byte> bytes)
    {
        const ulong offset = 14695981039346656037UL;
        const ulong prime = 1099511628211UL;
        var hash = offset;
        foreach (var value in bytes)
        {
            hash ^= value;
            hash *= prime;
        }

        return hash;
    }

    private readonly record struct HeaderCacheEntry(
        byte[] Bytes,
        ulong Hash,
        ZLinkEnvelopeHeader Header);
}
