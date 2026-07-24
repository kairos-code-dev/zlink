using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Contracts.Messaging;

public sealed class ZLinkMessage
{
    private readonly ZLinkCodecRegistrySnapshot? _codecs;
    private readonly Type? _declaredType;
    private readonly ReadOnlyMemory<byte> _payload;
    private readonly object? _value;

    private ZLinkMessage(object? value, Type? declaredType)
    {
        _value = value;
        _declaredType = declaredType;
    }

    private ZLinkMessage(
        ReadOnlyMemory<byte> payload,
        string? contentType,
        ZlinkStreamCodec? streamCodec,
        ZLinkCodecRegistrySnapshot? codecs)
    {
        _payload = payload;
        ContentType = contentType;
        StreamCodec = streamCodec;
        _codecs = codecs;
    }

    public string? ContentType { get; }

    public ZlinkStreamCodec? StreamCodec { get; }

    public bool IsEmpty => _declaredType is null && _payload.IsEmpty;

    public static ZLinkMessage Empty { get; } =
        new(ReadOnlyMemory<byte>.Empty, ZLinkEnvelopeCodec.DefaultContentType, null, null);

    public static ZLinkMessage From<T>(T value)
    {
        return new ZLinkMessage(value, typeof(T));
    }

    public T Decode<T>()
    {
        if (_declaredType is not null)
        {
            if (_value is T typed) return typed;

            if (_value is null) return default!;
        }

        var targetType = typeof(T);
        var decoded = Decode(targetType);
        return decoded is null ? default! : (T)decoded;
    }

    internal EncodedZLinkMessage Encode(ZLinkCodecRegistryBuilder codecs)
    {
        if (_declaredType is not null)
        {
            using var payload = ZLinkEnvelopeCodec.EncodeBody(_value, _declaredType, codecs);
            return new EncodedZLinkMessage(
                ZLinkEnvelopeCodec.ResolveContentType(_value, _declaredType, codecs),
                ZLinkEncodedPayload.From(payload.AsReadOnlyMemory()));
        }

        return new EncodedZLinkMessage(
            ContentType ?? ZLinkEnvelopeCodec.DefaultContentType,
            ZLinkEncodedPayload.From(_payload.Span));
    }

    internal ZLinkMessage Snapshot(ZLinkCodecRegistryBuilder codecs)
    {
        var encoded = Encode(codecs);
        return new ZLinkMessage(
            encoded.Payload.Bytes.ToArray(),
            encoded.ContentType,
            null,
            codecs.Snapshot());
    }

    internal static ZLinkMessage FromEncoded(
        string contentType,
        ReadOnlyMemory<byte> payload,
        ZLinkCodecRegistryBuilder codecs)
    {
        return new ZLinkMessage(payload.ToArray(), contentType, null, codecs.Snapshot());
    }

    internal Message ToRawMessage(ZLinkCodecRegistryBuilder codecs)
    {
        return Message.From(Encode(codecs).Payload.Bytes.Span);
    }

    internal static ZLinkMessage FromStreamPayload(
        ZlinkStreamCodec codec,
        ReadOnlyMemory<byte> payload,
        ZLinkCodecRegistryBuilder codecs)
    {
        var snapshot = codecs.Snapshot();
        var contentType = snapshot.TryResolveStreamContentType(codec, out var resolved)
            ? resolved
            : codec == ZlinkStreamCodec.Json
                ? ZLinkEnvelopeCodec.DefaultContentType
                : null;
        return new ZLinkMessage(payload, contentType, codec, snapshot);
    }

    internal static ZLinkMessage FromEnvelopePayload(
        string contentType,
        Message payload,
        ZLinkCodecRegistryBuilder codecs)
    {
        return new ZLinkMessage(
            payload.AsReadOnlyMemory().ToArray(),
            contentType,
            null,
            codecs.Snapshot());
    }

    private object? Decode(Type targetType)
    {
        if (StreamCodec == ZlinkStreamCodec.Raw)
        {
            if (targetType == typeof(string)) return Encoding.UTF8.GetString(_payload.Span);

            if (targetType == typeof(byte[])) return _payload.ToArray();
        }

        if (targetType == typeof(ReadOnlyMemory<byte>)) return _payload;

        if (targetType == typeof(byte[])) return _payload.ToArray();

        if (_payload.Length == 0) return targetType.IsValueType ? Activator.CreateInstance(targetType) : null;

        if (ContentType is not null
            && _codecs is not null
            && _codecs.TryGetSerializer(ContentType, out var serializer))
            return serializer.Deserialize(ZLinkEncodedPayload.From(_payload.Span), targetType);

        if (StreamCodec is { } codec
            && codec != ZlinkStreamCodec.Json)
            throw new InvalidOperationException(
                $"Stream payload uses codec '{codec}', but no matching codec extension is registered.");

        return JsonSerializer.Deserialize(
            _payload.Span,
            targetType,
            ZLinkJsonSerializerOptions.Default);
    }
}

internal readonly record struct EncodedZLinkMessage(string ContentType, ZLinkEncodedPayload Payload);
