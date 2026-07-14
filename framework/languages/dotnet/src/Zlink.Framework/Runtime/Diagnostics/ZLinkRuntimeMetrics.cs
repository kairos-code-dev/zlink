using System.Diagnostics;
using System.Diagnostics.Metrics;
using System.Collections.Concurrent;

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
    private static readonly Histogram<double> StreamSessionBindDuration =
        Meter.CreateHistogram<double>("zlink.stream.session.bind.duration", "s");
    private static readonly Counter<long> StreamInboundBytes =
        Meter.CreateCounter<long>("zlink.stream.inbound.bytes", "By");
    private static readonly Counter<long> StreamOutboundBytes =
        Meter.CreateCounter<long>("zlink.stream.outbound.bytes", "By");

    private static readonly UpDownCounter<long> SpotCount =
        Meter.CreateUpDownCounter<long>("zlink.spot.count", "{spot}");
    private static readonly UpDownCounter<long> SpotQueueDepth =
        Meter.CreateUpDownCounter<long>("zlink.spot.queue.depth", "{item}");
    private static readonly Histogram<double> SpotQueueWaitDuration =
        Meter.CreateHistogram<double>("zlink.spot.queue.wait.duration", "s");
    private static readonly Histogram<double> SpotTimerTickLateness =
        Meter.CreateHistogram<double>("zlink.spot.timer.tick.lateness", "s");
    private static readonly Counter<long> SpotCreated =
        Meter.CreateCounter<long>("zlink.spot.created", "{spot}");
    private static readonly Counter<long> SpotClosed =
        Meter.CreateCounter<long>("zlink.spot.closed", "{spot}");

    private static readonly UpDownCounter<long> ActorCount =
        Meter.CreateUpDownCounter<long>("zlink.actor.count", "{actor}");
    private static readonly UpDownCounter<long> ActorMailboxDepth =
        Meter.CreateUpDownCounter<long>("zlink.actor.mailbox.depth", "{item}");
    private static readonly Counter<long> ActorTransfers =
        Meter.CreateCounter<long>("zlink.actor.transfers", "{transfer}");
    private static readonly Histogram<double> ActorTransferDuration =
        Meter.CreateHistogram<double>("zlink.actor.transfer.duration", "s");
    private static readonly Histogram<long> ActorTransferPendingRequests =
        Meter.CreateHistogram<long>("zlink.actor.transfer.pending_requests.count", "{request}");

    private static readonly Histogram<double> ChannelRequestDuration =
        Meter.CreateHistogram<double>("zlink.channel.request.duration", "s");
    private static readonly UpDownCounter<long> ChannelRequestInflight =
        Meter.CreateUpDownCounter<long>("zlink.channel.request.inflight", "{request}");
    private static readonly Counter<long> ChannelRequestTimeouts =
        Meter.CreateCounter<long>("zlink.channel.request.timeouts", "{request}");
    private static readonly Counter<long> ChannelMessagesDropped =
        Meter.CreateCounter<long>("zlink.channel.messages.dropped", "{message}");

    private static readonly Counter<long> FanoutPublished =
        Meter.CreateCounter<long>("zlink.fanout.published", "{message}");
    private static readonly Counter<long> FanoutReceived =
        Meter.CreateCounter<long>("zlink.fanout.received", "{message}");

    private static readonly ConcurrentDictionary<object, Func<long>> LocationPeerProviders = new();
    private static readonly ObservableGauge<long> LocationPeers =
        Meter.CreateObservableGauge(
            "zlink.location.peers",
            ObserveLocationPeers,
            "{peer}");
    private static readonly Counter<long> LocationStoreErrors =
        Meter.CreateCounter<long>("zlink.location.store.errors", "{error}");
    private static readonly Counter<long> LocationOwnerLeaseRenewFailures =
        Meter.CreateCounter<long>("zlink.location.owner_lease.renew.failures", "{failure}");
    private static readonly Histogram<double> LocationOwnerLeaseRenewLateness =
        Meter.CreateHistogram<double>("zlink.location.owner_lease.renew.lateness", "s");
    private static readonly Counter<long> LocationWriteConflicts =
        Meter.CreateCounter<long>("zlink.location.write.conflicts", "{write}");

    private static readonly Counter<long> ObserverOverflow =
        Meter.CreateCounter<long>("zlink.observability.observer.overflow", "{event}");

    private static readonly ConcurrentDictionary<object, Func<string>> DrainStateProviders = new();
    private static readonly ObservableGauge<long> DrainState =
        Meter.CreateObservableGauge(
            "zlink.drain.state",
            ObserveDrainStates);
    private static readonly Histogram<double> DrainDuration =
        Meter.CreateHistogram<double>("zlink.drain.duration", "s");
    private static readonly Counter<long> DrainActorsHandedOff =
        Meter.CreateCounter<long>("zlink.drain.actors.handed_off", "{actor}");
    private static readonly Counter<long> DrainRoomsDrained =
        Meter.CreateCounter<long>("zlink.drain.rooms.drained", "{spot}");
    private static readonly Counter<long> DrainForced =
        Meter.CreateCounter<long>("zlink.drain.forced", "{item}");

    public static void RecordStreamOpened()
    {
        SafeAdd(StreamConnectionsActive, 1);
        SafeAdd(StreamConnectionsOpened, 1);
    }

    public static void RecordStreamClosed(string closeReason)
    {
        SafeAdd(StreamConnectionsActive, -1);
        SafeAdd(StreamConnectionsClosed, 1, "close_reason", closeReason);
    }

    public static void RecordStreamBytes(bool inbound, long bytes, string transport)
    {
        if (bytes <= 0) return;
        var counter = inbound ? StreamInboundBytes : StreamOutboundBytes;
        if (!counter.Enabled) return;
        SafeAdd(counter, bytes, "transport", transport);
    }

    public static long StartStreamSessionBind() => StartTimestamp(StreamSessionBindDuration);

    public static void CompleteStreamSessionBind(long startedTimestamp) =>
        RecordElapsed(StreamSessionBindDuration, startedTimestamp);

    public static void RecordSpotCreated(string kind)
    {
        SafeAdd(SpotCount, 1, "kind", kind);
        SafeAdd(SpotCreated, 1, "kind", kind);
    }

    public static void RecordSpotClosed(string kind)
    {
        SafeAdd(SpotCount, -1, "kind", kind);
        SafeAdd(SpotClosed, 1, "kind", kind);
    }

    public static long RecordSpotQueueEnqueued(string kind)
    {
        SafeAdd(SpotQueueDepth, 1, "kind", kind);
        return StartTimestamp(SpotQueueWaitDuration);
    }

    public static void RecordSpotQueueStarted(string kind, long startedTimestamp)
    {
        SafeAdd(SpotQueueDepth, -1, "kind", kind);
        RecordElapsed(SpotQueueWaitDuration, startedTimestamp, "kind", kind);
    }

    public static void RecordSpotQueueRemoved(string kind) =>
        SafeAdd(SpotQueueDepth, -1, "kind", kind);

    public static void RecordTimerLateness(TimeSpan lateness)
    {
        if (!SpotTimerTickLateness.Enabled) return;
        SafeRecord(SpotTimerTickLateness, Math.Max(0, lateness.TotalSeconds));
    }

    public static void RecordActorCreated() => SafeAdd(ActorCount, 1);
    public static void RecordActorClosed() => SafeAdd(ActorCount, -1);
    public static void RecordActorMailboxEnqueued() => SafeAdd(ActorMailboxDepth, 1);
    public static void RecordActorMailboxStarted() => SafeAdd(ActorMailboxDepth, -1);

    public static long StartActorTransfer(long pendingRequests)
    {
        if (ActorTransferPendingRequests.Enabled)
            SafeRecord(ActorTransferPendingRequests, Math.Max(0, pendingRequests));
        return StartTimestamp(ActorTransferDuration);
    }

    public static void CompleteActorTransfer(long startedTimestamp)
    {
        SafeAdd(ActorTransfers, 1);
        RecordElapsed(ActorTransferDuration, startedTimestamp);
    }

    public static long StartChannelRequest()
    {
        SafeAdd(ChannelRequestInflight, 1);
        return StartTimestamp(ChannelRequestDuration);
    }

    public static void CompleteChannelRequest(long startedTimestamp, bool timedOut)
    {
        SafeAdd(ChannelRequestInflight, -1);
        RecordElapsed(ChannelRequestDuration, startedTimestamp);
        if (timedOut) SafeAdd(ChannelRequestTimeouts, 1);
    }

    public static void RecordChannelDropped(string surface, string kind, string reason)
    {
        if (!ChannelMessagesDropped.Enabled) return;
        try
        {
            ChannelMessagesDropped.Add(
                1,
                new KeyValuePair<string, object?>("surface", surface),
                new KeyValuePair<string, object?>("kind", kind),
                new KeyValuePair<string, object?>("reason", reason));
        }
        catch
        {
        }
    }

    public static void RecordFanoutPublished(string? topic) => RecordTopic(FanoutPublished, topic);
    public static void RecordFanoutReceived(string? topic) => RecordTopic(FanoutReceived, topic);

    public static IDisposable RegisterLocationPeers(Func<long> snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var owner = new object();
        LocationPeerProviders[owner] = snapshot;
        return new ProviderRegistration(() => LocationPeerProviders.TryRemove(owner, out _));
    }

    public static IDisposable RegisterDrainState(Func<string> snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var owner = new object();
        DrainStateProviders[owner] = snapshot;
        return new ProviderRegistration(() => DrainStateProviders.TryRemove(owner, out _));
    }

    public static void RecordLocationStoreError() => SafeAdd(LocationStoreErrors, 1);

    public static void RecordOwnerLeaseRenewFailure() =>
        SafeAdd(LocationOwnerLeaseRenewFailures, 1);

    public static void RecordOwnerLeaseRenewLateness(TimeSpan lateness)
    {
        if (!LocationOwnerLeaseRenewLateness.Enabled) return;
        SafeRecord(LocationOwnerLeaseRenewLateness, Math.Max(0, lateness.TotalSeconds));
    }

    public static void RecordLocationWriteConflict() => SafeAdd(LocationWriteConflicts, 1);

    public static void RecordOwnerLeaseRenewAttempt(TimeProvider timeProvider, long scheduledTimestamp)
    {
        if (scheduledTimestamp == 0 || !LocationOwnerLeaseRenewLateness.Enabled) return;
        var elapsedTicks = timeProvider.GetTimestamp() - scheduledTimestamp;
        if (elapsedTicks <= 0) return;
        SafeRecord(
            LocationOwnerLeaseRenewLateness,
            elapsedTicks / (double)timeProvider.TimestampFrequency);
    }

    public static void RecordObserverOverflow(string eventName)
    {
        if (!ObserverOverflow.Enabled) return;
        SafeAdd(ObserverOverflow, 1, "event", eventName);
    }

    public static long StartDrain() => StartTimestamp(DrainDuration);

    public static void CompleteDrain(long startedTimestamp, string outcome)
    {
        if (startedTimestamp == 0 || !DrainDuration.Enabled) return;
        SafeRecord(
            DrainDuration,
            Stopwatch.GetElapsedTime(startedTimestamp).TotalSeconds,
            "outcome",
            outcome);
    }

    public static void RecordDrainActorHandedOff() => SafeAdd(DrainActorsHandedOff, 1);

    public static void RecordDrainRoom(string policy) =>
        SafeAdd(DrainRoomsDrained, 1, "policy", policy);

    public static void RecordDrainForced(string kind, long count = 1)
    {
        if (count > 0) SafeAdd(DrainForced, count, "kind", kind);
    }

    private static long ObserveLocationPeers()
    {
        long total = 0;
        foreach (var snapshot in LocationPeerProviders.Values)
            try
            {
                total += Math.Max(0, snapshot());
            }
            catch
            {
            }
        return total;
    }

    private static IEnumerable<Measurement<long>> ObserveDrainStates()
    {
        foreach (var snapshot in DrainStateProviders.Values)
        {
            string state;
            try
            {
                state = snapshot();
            }
            catch
            {
                continue;
            }
            yield return new Measurement<long>(
                1,
                new KeyValuePair<string, object?>("state", state));
        }
    }

    private sealed class ProviderRegistration(Action unregister) : IDisposable
    {
        private Action? _unregister = unregister;

        public void Dispose() => Interlocked.Exchange(ref _unregister, null)?.Invoke();
    }

    private static void RecordTopic(Counter<long> counter, string? topic)
    {
        if (!counter.Enabled) return;
        try
        {
            if (topic is null) counter.Add(1);
            else counter.Add(1, new KeyValuePair<string, object?>("topic", topic));
        }
        catch
        {
        }
    }

    private static long StartTimestamp(Histogram<double> histogram) =>
        histogram.Enabled ? Stopwatch.GetTimestamp() : 0;

    private static void RecordElapsed(Histogram<double> histogram, long startedTimestamp)
    {
        if (startedTimestamp == 0 || !histogram.Enabled) return;
        SafeRecord(histogram, Stopwatch.GetElapsedTime(startedTimestamp).TotalSeconds);
    }

    private static void RecordElapsed(
        Histogram<double> histogram,
        long startedTimestamp,
        string tagName,
        object? tagValue)
    {
        if (startedTimestamp == 0 || !histogram.Enabled) return;
        SafeRecord(
            histogram,
            Stopwatch.GetElapsedTime(startedTimestamp).TotalSeconds,
            tagName,
            tagValue);
    }

    private static void SafeAdd(Counter<long> counter, long value)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(value);
        }
        catch
        {
        }
    }

    private static void SafeAdd(
        Counter<long> counter,
        long value,
        string tagName,
        object? tagValue)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(value, new KeyValuePair<string, object?>(tagName, tagValue));
        }
        catch
        {
        }
    }

    private static void SafeAdd(UpDownCounter<long> counter, long value)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(value);
        }
        catch
        {
        }
    }

    private static void SafeAdd(
        UpDownCounter<long> counter,
        long value,
        string tagName,
        object? tagValue)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(value, new KeyValuePair<string, object?>(tagName, tagValue));
        }
        catch
        {
        }
    }

    private static void SafeRecord(Histogram<double> histogram, double value)
    {
        if (!histogram.Enabled) return;
        try
        {
            histogram.Record(value);
        }
        catch
        {
        }
    }

    private static void SafeRecord(
        Histogram<double> histogram,
        double value,
        string tagName,
        object? tagValue)
    {
        if (!histogram.Enabled) return;
        try
        {
            histogram.Record(value, new KeyValuePair<string, object?>(tagName, tagValue));
        }
        catch
        {
        }
    }

    private static void SafeRecord(Histogram<long> histogram, long value)
    {
        if (!histogram.Enabled) return;
        try
        {
            histogram.Record(value);
        }
        catch
        {
        }
    }
}
