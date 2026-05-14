using System.Text.Json;

namespace Zlink.Framework.Messaging;

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
        return ZLinkMessageParts.Create(
            EncodeHeader(header),
            EncodeBody(body, bodyType));
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
        if (bodyType is null || body is null)
        {
            return Message.FromBytes(ReadOnlySpan<byte>.Empty);
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
        EnsurePart(parts, 1, "body");
        return DecodeBody(parts[1], bodyType);
    }

    public static object? DecodeBody(Message bodyMessage, Type bodyType)
    {
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
        return Message.FromBytes(EncodeJsonBytes(value));
    }

    public static Message EncodeJsonPart(object? value, Type valueType)
    {
        return Message.FromBytes(EncodeJsonBytes(value, valueType));
    }

    public static byte[] EncodeJsonBytes<T>(T value)
        => JsonSerializer.SerializeToUtf8Bytes(value, ZLinkJsonSerializerOptions.Default);

    public static byte[] EncodeJsonBytes(object? value, Type valueType)
        => JsonSerializer.SerializeToUtf8Bytes(value, valueType, ZLinkJsonSerializerOptions.Default);

    private static void EnsurePart(IReadOnlyList<Message> parts, int index, string name)
    {
        if (parts.Count <= index)
        {
            throw new InvalidOperationException($"ZLink envelope {name} part is missing.");
        }
    }
}
