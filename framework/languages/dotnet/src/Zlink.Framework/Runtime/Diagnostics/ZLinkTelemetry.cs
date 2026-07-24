using System.Diagnostics;
using System.Diagnostics.Metrics;

namespace Zlink.Framework.Runtime.Diagnostics;

internal static class ZLinkTelemetry
{
    public const string ActivitySourceName = "Zlink.Framework";
    public const string MeterName = ZLinkMeters.Framework;

    public static readonly ActivitySource ActivitySource = new(ActivitySourceName);

    public static string CaptureSubmitOperationId() =>
        Activity.Current?.Id ?? Guid.NewGuid().ToString("N");

    public static void TraceSubmitAdmission(
        string operationId,
        string eventName,
        int pendingWaiterCount,
        bool retry = false)
    {
        if (!ActivitySource.HasListeners()) return;

        using var activity = ActivitySource.StartActivity(
            "zlink.submit.admission",
            ActivityKind.Internal);
        if (activity is null) return;

        activity.SetTag("zlink.submit.operation_id", operationId);
        activity.SetTag("zlink.submit.event", eventName);
        activity.SetTag("zlink.submit.retry", retry);
        activity.SetTag("zlink.submit.pending_waiters", pendingWaiterCount);
        activity.SetTag("zlink.submit.reservations", pendingWaiterCount);
        activity.SetTag("zlink.submit.callbacks", 0);
        activity.AddEvent(new ActivityEvent(eventName));
    }

    public static void RecordHandlerMissing(
        string surface,
        string kind,
        string action,
        string reason)
    {
        if (IsChannelSurface(surface)
            && string.Equals(action, "drop", StringComparison.Ordinal))
            ZLinkRuntimeMetrics.RecordChannelDropped(surface, kind, "no_handler");
    }

    public static void RecordDropped(
        string surface,
        string kind,
        string reason)
    {
        if (!IsChannelSurface(surface)) return;
        if (NormalizeDropReason(reason) is { } normalized)
            ZLinkRuntimeMetrics.RecordChannelDropped(surface, kind, normalized);
    }

    public static void RecordReplyError(
        string surface,
        string kind,
        string reason)
    {
        _ = surface;
        _ = kind;
        _ = reason;
    }

    private static string? NormalizeDropReason(string reason)
    {
        return reason switch
        {
            "handler-missing" or "no-handler" => "no_handler",
            "payload-decode-failed" or "invalid-frame" => "decode_error",
            "backpressure" => "backpressure",
            "stale-route" => "stale_route",
            _ => null
        };
    }

    private static bool IsChannelSurface(string surface) =>
        string.Equals(surface, "Channel", StringComparison.Ordinal)
        || string.Equals(surface, "RouteMeshChannel", StringComparison.Ordinal);

    public static void TraceFlowEvent(
        string eventName,
        ZLinkMessageFlowEvent flow,
        string action,
        string reason,
        string? surfaceName = null,
        string? kindName = null,
        string? actorType = null)
    {
        if (!ActivitySource.HasListeners()) return;

        surfaceName ??= flow.Surface.ToString();
        kindName ??= flow.MessageKind.ToString();
        using var activity = ActivitySource.StartActivity(
            ResolveSpanName(flow.Surface),
            ActivityKind.Consumer);
        if (activity is null) return;

        activity.SetTag("zlink.surface", surfaceName);
        activity.SetTag("zlink.kind", kindName);
        activity.SetTag("zlink.packet.name", flow.PacketName);
        activity.SetTag("zlink.action", action);
        activity.SetTag("zlink.reason", reason);
        if (!string.IsNullOrEmpty(flow.ChannelName)) activity.SetTag("zlink.channel.name", flow.ChannelName);
        if (!string.IsNullOrEmpty(flow.ActorId)) activity.SetTag("zlink.actor.id", flow.ActorId);
        if (!string.IsNullOrEmpty(actorType)) activity.SetTag("zlink.actor.type", actorType);
        if (!string.IsNullOrEmpty(flow.SpotId)) activity.SetTag("zlink.spot.rid", flow.SpotId);
        activity.AddEvent(new ActivityEvent(eventName));
    }

    private static string ResolveSpanName(ZLinkDispatchErrorSurface surface)
    {
        return surface switch
        {
            ZLinkDispatchErrorSurface.StreamSession => "zlink.session.dispatch",
            ZLinkDispatchErrorSurface.Channel => "zlink.channel.dispatch",
            ZLinkDispatchErrorSurface.RouteMeshChannel => "zlink.route.dispatch",
            ZLinkDispatchErrorSurface.SpotRoute or ZLinkDispatchErrorSurface.SpotSubscription => "zlink.spot.dispatch",
            ZLinkDispatchErrorSurface.SpotActor => "zlink.actor.dispatch",
            _ => "zlink.message.dispatch"
        };
    }
}
