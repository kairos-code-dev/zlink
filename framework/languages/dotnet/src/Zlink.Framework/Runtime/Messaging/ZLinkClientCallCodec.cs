using System.Text.Json;

namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkClientCallCodec
{
    public static ZLinkEnvelopeHeader CreateEnvelope(
        ZLinkMessageKind kind,
        string channelName,
        string messageName,
        TimeSpan? timeout = null,
        string? topic = null,
        string? source = null,
        bool includeCorrelationId = true,
        bool includeDeadline = true)
    {
        return new ZLinkEnvelopeHeader(
            kind,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            includeCorrelationId ? Guid.NewGuid().ToString("N") : null,
            includeDeadline && timeout is { } value ? DateTimeOffset.UtcNow.Add(value) : null,
            topic,
            null,
            null,
            source);
    }

    public static IReadOnlyList<Message> EncodeEnvelopeParts<TMessage>(
        ZLinkEnvelopeHeader header,
        TMessage message)
    {
        return EncodeEnvelopeParts(header, message, null);
    }

    public static IReadOnlyList<Message> EncodeEnvelopeParts<TMessage>(
        ZLinkEnvelopeHeader header,
        TMessage message,
        ZLinkCodecRegistryBuilder? codecs)
    {
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            ZLinkClientCallTypeCache<TMessage>.Resolve(message),
            codecs);
    }

    public static TReply DecodeEnvelopeReply<TReply>(
        IReadOnlyList<Message> reply,
        string emptyMessage,
        string errorMessage)
    {
        return DecodeEnvelopeReply<TReply>(reply, emptyMessage, errorMessage, null);
    }

    public static TReply DecodeEnvelopeReply<TReply>(
        IReadOnlyList<Message> reply,
        string emptyMessage,
        string errorMessage,
        ZLinkCodecRegistryBuilder? codecs)
    {
        if (reply.Count == 0) throw new InvalidOperationException(emptyMessage);

        var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
        if (replyHeader.Kind == ZLinkMessageKind.Error)
            throw new InvalidOperationException(replyHeader.ErrorMessage ?? errorMessage);

        return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply, typeof(TReply), replyHeader.ContentType, codecs)
               ?? throw new InvalidOperationException("Reply body is null.");
    }

    public static TReply DecodeEnvelopeReplyAndDispose<TReply>(
        IReadOnlyList<Message> reply,
        string emptyMessage,
        string errorMessage)
    {
        return DecodeEnvelopeReplyAndDispose<TReply>(reply, emptyMessage, errorMessage, null);
    }

    public static TReply DecodeEnvelopeReplyAndDispose<TReply>(
        IReadOnlyList<Message> reply,
        string emptyMessage,
        string errorMessage,
        ZLinkCodecRegistryBuilder? codecs)
    {
        try
        {
            return DecodeEnvelopeReply<TReply>(reply, emptyMessage, errorMessage, codecs);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    public static TReply DecodeJsonReply<TReply>(
        ReadOnlySpan<byte> reply,
        string nullMessage)
    {
        return JsonSerializer.Deserialize<TReply>(reply, ZLinkJsonSerializerOptions.Default)
               ?? throw new InvalidOperationException(nullMessage);
    }

    public static Dictionary<string, string> CopyMetadata(
        IReadOnlyDictionary<string, string> metadata)
    {
        return new Dictionary<string, string>(metadata, StringComparer.Ordinal);
    }
}

internal static class ZLinkClientCallTypeCache<TMessage>
{
    private static readonly Type StaticType = typeof(TMessage);

    public static Type Resolve(TMessage message)
    {
        if (message is null) return StaticType;
        return StaticType.IsSealed ? StaticType : message.GetType();
    }
}
