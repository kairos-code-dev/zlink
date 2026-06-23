using System.Text.Json;

namespace Zlink.Framework.Runtime.Messaging;

internal enum ZLinkMessageKind
{
    Request = 1,
    Response = 2,
    Command = 3,
    Publish = 4,
    Error = 5,
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
        return EncodeJsonPart(header);
    }

    public static Message EncodeBody(object? body, Type? bodyType)
    {
        return EncodeBody(body, bodyType, null);
    }

    public static Message EncodeBody(object? body, Type? bodyType, ZLinkCodecRegistryBuilder? codecs)
    {
        if (bodyType is null || body is null)
        {
            return Message.From(ReadOnlySpan<byte>.Empty);
        }

        if (bodyType == typeof(Message))
        {
            if (body is not Message message)
            {
                throw new InvalidOperationException(
                    $"Envelope body type is Message, but body instance is '{body.GetType()}'.");
            }

            return Message.From(message);
        }

        if (bodyType == typeof(ZLinkMessage))
        {
            if (body is not ZLinkMessage message)
            {
                throw new InvalidOperationException(
                    $"Envelope body type is ZLinkMessage, but body instance is '{body.GetType()}'.");
            }

            return message.Encode(codecs ?? new ZLinkCodecRegistryBuilder()).Message;
        }

        if (codecs is not null
            && codecs.TryResolveSerializer(bodyType, out _, out var serializer))
        {
            return serializer.Serialize(body, bodyType);
        }

        if (codecs?.SingleCustomSerializer() is { } custom)
        {
            return custom.Serializer.Serialize(body, bodyType);
        }

        return EncodeJsonPart(body, bodyType);
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
        return JsonSerializer.Deserialize<ZLinkEnvelopeHeader>(
                message.AsReadOnlySpan(),
                ZLinkJsonSerializerOptions.Default)
            ?? throw new InvalidOperationException("Invalid ZLink envelope header.");
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
        EnsurePart(parts, 1, "body");
        return DecodeBody(parts[1], bodyType, DecodeHeader(parts).ContentType, codecs);
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
        if (bodyType == typeof(Message))
        {
            return bodyMessage;
        }

        if (bodyType == typeof(ZLinkMessage))
        {
            return ZLinkMessage.FromEnvelopePayload(contentType, bodyMessage, codecs ?? new ZLinkCodecRegistryBuilder());
        }

        if (bodyType == typeof(ReadOnlyMemory<byte>))
        {
            return bodyMessage.AsReadOnlyMemory();
        }

        if (bodyMessage.Size == 0)
        {
            return bodyType.IsValueType
                ? Activator.CreateInstance(bodyType)
                : null;
        }

        if (codecs is not null
            && codecs.TryGetSerializer(contentType, out var customSerializer))
        {
            return customSerializer.Deserialize(bodyMessage, bodyType);
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

    public static string DefaultContentType => JsonContentType;

    public static Message EncodeJsonPart<T>(T value)
    {
        return Message.From(EncodeJsonBytes(value));
    }

    public static Message EncodeJsonPart(object? value, Type valueType)
    {
        return Message.From(EncodeJsonBytes(value, valueType));
    }

    public static byte[] EncodeJsonBytes<T>(T value)
        => JsonSerializer.SerializeToUtf8Bytes(value, ZLinkJsonSerializerOptions.Default);

    public static byte[] EncodeJsonBytes(object? value, Type valueType)
        => JsonSerializer.SerializeToUtf8Bytes(value, valueType, ZLinkJsonSerializerOptions.Default);

    private static string ResolveContentType(object? body, Type? bodyType, ZLinkCodecRegistryBuilder? codecs)
    {
        if (body is null || bodyType is null)
        {
            return JsonContentType;
        }

        if (bodyType == typeof(Message) || body is Message)
        {
            return JsonContentType;
        }

        if (bodyType == typeof(ZLinkMessage) && body is ZLinkMessage message)
        {
            var encoded = message.Encode(codecs ?? new ZLinkCodecRegistryBuilder());
            encoded.Message.Dispose();
            return encoded.ContentType;
        }

        if (codecs is not null
            && codecs.TryResolveSerializer(bodyType, out var contentType, out _))
        {
            return contentType;
        }

        if (codecs?.SingleCustomSerializer() is { } custom)
        {
            return custom.ContentType;
        }

        return JsonContentType;
    }
    private static void EnsurePart(IReadOnlyList<Message> parts, int index, string name)
    {
        if (parts.Count <= index)
        {
            throw new InvalidOperationException($"ZLink envelope {name} part is missing.");
        }
    }
}
