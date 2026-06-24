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

        if (string.Equals(action, "reply-error", StringComparison.Ordinal))
        {
            RecordReplyError(surface, kind, reason);
        }
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
        if (!ActivitySource.HasListeners())
        {
            return;
        }

        using var activity = ActivitySource.StartActivity(
            ResolveSpanName(surface),
            ActivityKind.Consumer);
        if (activity is null)
        {
            return;
        }

        activity.SetTag("zlink.surface", surface);
        activity.SetTag("zlink.kind", kind);
        activity.SetTag("zlink.packet.name", packetName);
        activity.SetTag("zlink.action", action);
        activity.SetTag("zlink.reason", reason);
        if (!string.IsNullOrEmpty(channelName))
        {
            activity.SetTag("zlink.channel.name", channelName);
        }
        if (!string.IsNullOrEmpty(actorId))
        {
            activity.SetTag("zlink.actor.id", actorId);
        }
        if (!string.IsNullOrEmpty(actorType))
        {
            activity.SetTag("zlink.actor.type", actorType);
        }
        if (!string.IsNullOrEmpty(spotRid))
        {
            activity.SetTag("zlink.spot.rid", spotRid);
        }
        activity.AddEvent(new ActivityEvent(eventName));
    }

    private static string ResolveSpanName(string surface)
    {
        return surface switch
        {
            "Session" => "zlink.session.dispatch",
            "Channel" => "zlink.channel.dispatch",
            "RouteMeshChannel" => "zlink.route.dispatch",
            "Spot" => "zlink.spot.dispatch",
            "Actor" => "zlink.actor.dispatch",
            "EntrySpot" => "zlink.actor.dispatch",
            _ => "zlink.message.dispatch"
        };
    }
}
