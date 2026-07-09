using System.Diagnostics;
using System.Diagnostics.Metrics;

namespace Zlink.Framework.Runtime.Diagnostics;

internal static class ZLinkTelemetry
{
    public const string ActivitySourceName = "Zlink.Framework";
    public const string MeterName = "Zlink.Framework";

    public static readonly ActivitySource ActivitySource = new(ActivitySourceName);
    public static readonly Meter Meter = new(MeterName);

    private static readonly Counter<long> HandlerMissingCounter =
        Meter.CreateCounter<long>("zlink.messages.handler_missing");

    private static readonly Counter<long> DroppedCounter =
        Meter.CreateCounter<long>("zlink.messages.dropped");

    private static readonly Counter<long> ReplyErrorCounter =
        Meter.CreateCounter<long>("zlink.messages.reply_error");

    public static void RecordHandlerMissing(
        string surface,
        string kind,
        string action,
        string reason)
    {
        HandlerMissingCounter.Add(
            1,
            new KeyValuePair<string, object?>("surface", surface),
            new KeyValuePair<string, object?>("kind", kind),
            new KeyValuePair<string, object?>("action", action),
            new KeyValuePair<string, object?>("reason", reason));

        if (string.Equals(action, "reply-error", StringComparison.Ordinal)) RecordReplyError(surface, kind, reason);
    }

    public static void RecordDropped(
        string surface,
        string kind,
        string reason)
    {
        DroppedCounter.Add(
            1,
            new KeyValuePair<string, object?>("surface", surface),
            new KeyValuePair<string, object?>("kind", kind),
            new KeyValuePair<string, object?>("action", "drop"),
            new KeyValuePair<string, object?>("reason", reason));
    }

    public static void RecordReplyError(
        string surface,
        string kind,
        string reason)
    {
        ReplyErrorCounter.Add(
            1,
            new KeyValuePair<string, object?>("surface", surface),
            new KeyValuePair<string, object?>("kind", kind),
            new KeyValuePair<string, object?>("action", "reply-error"),
            new KeyValuePair<string, object?>("reason", reason));
    }

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
        if (!string.IsNullOrEmpty(flow.SpotRid)) activity.SetTag("zlink.spot.rid", flow.SpotRid);
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
