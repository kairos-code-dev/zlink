using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.Messaging;

internal enum ZLinkMessageKind
{
    Request = 1,
    Response = 2,
    Command = 3,
    Event = 4,
    Error = 5,
}

internal sealed record ZLinkEnvelopeHeader(
    ZLinkMessageKind Kind,
    string ChannelName,
    string PacketName,
    string ContentType,
    string? CorrelationId,
    DateTimeOffset? Deadline,
    string? Topic,
    string? ErrorCode,
    string? ErrorMessage);

internal static class ZLinkEnvelopeCodec
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private const string JsonContentType = "application/json";

    public static global::Zlink.Message Encode(
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        var envelope = new ZLinkSerializedEnvelope(
            header,
            bodyType is not null && body is not null
                ? JsonSerializer.SerializeToElement(body, bodyType, JsonOptions)
                : null);
        return global::Zlink.Message.FromString(
            JsonSerializer.Serialize(envelope, JsonOptions));
    }

    public static ZLinkEnvelopeHeader DecodeHeader(global::Zlink.Message message)
    {
        return DecodeEnvelope(message).Header;
    }

    public static object? DecodeBody(global::Zlink.Message message, Type bodyType)
    {
        var envelope = DecodeEnvelope(message);
        if (envelope.Body is null)
        {
            return bodyType.IsValueType
                ? Activator.CreateInstance(bodyType)
                : null;
        }

        return envelope.Body.Value.Deserialize(bodyType, JsonOptions);
    }

    private static ZLinkSerializedEnvelope DecodeEnvelope(global::Zlink.Message message)
    {
        var envelope = JsonSerializer.Deserialize<ZLinkSerializedEnvelope>(
            message.AsReadOnlySpan(),
            JsonOptions);

        return envelope ?? throw new InvalidOperationException("Invalid ZLink envelope header.");
    }

    public static string DefaultContentType => JsonContentType;

    private sealed record ZLinkSerializedEnvelope(
        ZLinkEnvelopeHeader Header,
        JsonElement? Body);
}
