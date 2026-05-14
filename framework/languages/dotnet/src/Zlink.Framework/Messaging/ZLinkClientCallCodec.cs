using System.Text.Json;

namespace Zlink.Framework.Messaging;

internal static class ZLinkClientCallCodec
{
    public static ZLinkEnvelopeHeader CreateEnvelope(
        ZLinkMessageKind kind,
        string channelName,
        string messageName,
        TimeSpan? timeout = null,
        string? topic = null,
        string? source = null)
    {
        return new ZLinkEnvelopeHeader(
            kind,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            timeout is { } value ? DateTimeOffset.UtcNow.Add(value) : null,
            topic,
            null,
            null,
            source);
    }

    public static IReadOnlyList<Message> EncodeEnvelopeParts<TMessage>(
        ZLinkEnvelopeHeader header,
        TMessage message)
    {
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            message?.GetType() ?? typeof(TMessage));
    }

    public static TReply DecodeEnvelopeReply<TReply>(
        IReadOnlyList<Message> reply,
        string emptyMessage,
        string errorMessage)
    {
        if (reply.Count == 0)
        {
            throw new InvalidOperationException(emptyMessage);
        }

        var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
        if (replyHeader.Kind == ZLinkMessageKind.Error)
        {
            throw new InvalidOperationException(replyHeader.ErrorMessage ?? errorMessage);
        }

        return (TReply?)ZLinkEnvelopeCodec.DecodeBody(reply, typeof(TReply))
            ?? throw new InvalidOperationException("Reply body is null.");
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
