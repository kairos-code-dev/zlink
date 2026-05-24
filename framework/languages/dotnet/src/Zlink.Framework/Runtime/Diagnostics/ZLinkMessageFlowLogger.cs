using Microsoft.Extensions.Logging;

namespace Zlink.Framework.Runtime.Diagnostics;

internal static class ZLinkMessageFlowLogger
{
    private static readonly EventId HandlerMissingEvent = new(1001, "ZLinkHandlerMissing");
    private static readonly EventId PayloadDecodeFailedEvent = new(1002, "ZLinkPayloadDecodeFailed");
    private static readonly EventId MessageDroppedEvent = new(1003, "ZLinkMessageDropped");
    private static readonly EventId MessageRejectedEvent = new(1004, "ZLinkMessageRejected");

    public static void HandlerMissing(
        ILogger logger,
        LogLevel level,
        string surface,
        string kind,
        string packetName,
        string action,
        string reason,
        string? channelName = null,
        string? actorId = null,
        string? actorType = null,
        string? spotRid = null)
    {
        ZLinkTelemetry.RecordHandlerMissing(surface, kind, action, reason);
        ZLinkTelemetry.TraceFlowEvent(
            "handler-missing",
            surface,
            kind,
            packetName,
            action,
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);

        if (!logger.IsEnabled(level))
        {
            return;
        }

        logger.Log(
            level,
            HandlerMissingEvent,
            "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason} {ChannelName} {ActorId} {ActorType} {SpotRid}",
            "handler-missing",
            surface,
            kind,
            packetName,
            action,
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);
    }

    public static void PayloadDecodeFailed(
        ILogger logger,
        string surface,
        string kind,
        string packetName,
        string action,
        string reason,
        Exception exception,
        string? channelName = null,
        string? actorId = null,
        string? actorType = null,
        string? spotRid = null)
    {
        if (string.Equals(action, "reply-error", StringComparison.Ordinal))
        {
            ZLinkTelemetry.RecordReplyError(surface, kind, reason);
        }
        ZLinkTelemetry.TraceFlowEvent(
            "failed",
            surface,
            kind,
            packetName,
            action,
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);

        if (!logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        logger.LogWarning(
            PayloadDecodeFailedEvent,
            exception,
            "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason} {ChannelName} {ActorId} {ActorType} {SpotRid}",
            "failed",
            surface,
            kind,
            packetName,
            action,
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);
    }

    public static void Dropped(
        ILogger logger,
        LogLevel level,
        string surface,
        string kind,
        string packetName,
        string reason,
        string? channelName = null,
        string? actorId = null,
        string? actorType = null,
        string? spotRid = null)
    {
        ZLinkTelemetry.RecordDropped(surface, kind, reason);
        ZLinkTelemetry.TraceFlowEvent(
            "dropped",
            surface,
            kind,
            packetName,
            "drop",
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);

        if (!logger.IsEnabled(level))
        {
            return;
        }

        logger.Log(
            level,
            MessageDroppedEvent,
            "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason} {ChannelName} {ActorId} {ActorType} {SpotRid}",
            "dropped",
            surface,
            kind,
            packetName,
            "drop",
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);
    }

    public static void Rejected(
        ILogger logger,
        LogLevel level,
        string surface,
        string kind,
        string packetName,
        string reason,
        Exception? exception = null,
        string? channelName = null,
        string? actorId = null,
        string? actorType = null,
        string? spotRid = null)
    {
        if (string.Equals(reason, "no-join-handler", StringComparison.Ordinal))
        {
            ZLinkTelemetry.RecordHandlerMissing(surface, kind, "rejected", reason);
        }
        ZLinkTelemetry.TraceFlowEvent(
            "rejected",
            surface,
            kind,
            packetName,
            "rejected",
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);

        if (!logger.IsEnabled(level))
        {
            return;
        }

        logger.Log(
            level,
            MessageRejectedEvent,
            exception,
            "ZLink message flow {Event} {Surface} {Kind} {PacketName} {Action} {Reason} {ChannelName} {ActorId} {ActorType} {SpotRid}",
            "rejected",
            surface,
            kind,
            packetName,
            "rejected",
            reason,
            channelName,
            actorId,
            actorType,
            spotRid);
    }
}
