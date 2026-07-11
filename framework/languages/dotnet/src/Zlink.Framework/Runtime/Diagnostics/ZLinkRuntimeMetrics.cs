using System.Diagnostics.Metrics;

namespace Zlink.Framework.Runtime.Diagnostics;

internal static class ZLinkRuntimeMetrics
{
    private static readonly Meter Meter = new(ZLinkMeters.Framework);

    private static readonly UpDownCounter<long> StreamConnectionsActive =
        Meter.CreateUpDownCounter<long>("zlink.stream.connections.active", "{connection}");

    private static readonly Counter<long> StreamConnectionsOpened =
        Meter.CreateCounter<long>("zlink.stream.connections.opened", "{connection}");

    private static readonly Counter<long> StreamConnectionsClosed =
        Meter.CreateCounter<long>("zlink.stream.connections.closed", "{connection}");

    private static readonly Counter<long> ChannelMessagesDropped =
        Meter.CreateCounter<long>("zlink.channel.messages.dropped", "{message}");

    private static readonly Counter<long> ObserverOverflow =
        Meter.CreateCounter<long>("zlink.observability.observer.overflow", "{event}");

    public static void RecordStreamOpened()
    {
        StreamConnectionsActive.Add(1);
        StreamConnectionsOpened.Add(1);
    }

    public static void RecordStreamClosed(string closeReason)
    {
        StreamConnectionsActive.Add(-1);
        StreamConnectionsClosed.Add(1, new KeyValuePair<string, object?>("close_reason", closeReason));
    }

    public static void RecordChannelDropped(string surface, string kind, string reason)
    {
        ChannelMessagesDropped.Add(
            1,
            new KeyValuePair<string, object?>("surface", surface),
            new KeyValuePair<string, object?>("kind", kind),
            new KeyValuePair<string, object?>("reason", reason));
    }

    public static void RecordObserverOverflow(string eventName)
    {
        ObserverOverflow.Add(1, new KeyValuePair<string, object?>("event", eventName));
    }
}
