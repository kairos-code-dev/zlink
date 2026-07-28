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
    private static readonly Histogram<double> SpotTimerTickLateness =
        Meter.CreateHistogram<double>("zlink.spot.timer.tick.lateness", "s");
    private static readonly Counter<long> SpotCreated =
        Meter.CreateCounter<long>("zlink.spot.created", "{spot}");
    private static readonly Counter<long> SpotClosed =
        Meter.CreateCounter<long>("zlink.spot.closed", "{spot}");

    private static readonly UpDownCounter<long> ActorCount =
        Meter.CreateUpDownCounter<long>("zlink.actor.count", "{actor}");
    private static readonly Counter<long> RelocationStarted =
        Meter.CreateCounter<long>("zlink.relocation.started", "{relocation}");
    private static readonly Counter<long> RelocationCompleted =
        Meter.CreateCounter<long>("zlink.relocation.completed", "{relocation}");
    private static readonly Histogram<double> RelocationDuration =
        Meter.CreateHistogram<double>("zlink.relocation.duration", "s");
    private static readonly Counter<long> RelocationRecovered =
        Meter.CreateCounter<long>("zlink.relocation.recovered", "{relocation}");
    private static readonly Histogram<long> RelocationJournalMessages =
        Meter.CreateHistogram<long>("zlink.relocation.journal.messages", "{message}");
    private static readonly Histogram<long> RelocationBytes =
        Meter.CreateHistogram<long>("zlink.relocation.bytes", "By");
    private static readonly Histogram<double> RelocationInterruption =
        Meter.CreateHistogram<double>(
            "zlink.relocation.interruption",
            "s");

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
    private static readonly Counter<long> DrainForced =
        Meter.CreateCounter<long>("zlink.drain.forced", "{item}");
    private static readonly ConcurrentDictionary<object, Func<string>> TerminationStateProviders =
        new();
    private static readonly ObservableGauge<long> TerminationState =
        Meter.CreateObservableGauge(
            "zlink.termination.state",
            ObserveTerminationStates);
    private static readonly Histogram<double> TerminationDuration =
        Meter.CreateHistogram<double>("zlink.termination.duration", "s");
    private static readonly Counter<long> TerminationBlocked =
        Meter.CreateCounter<long>("zlink.termination.blocked", "{operation}");
    private static readonly Counter<long> TerminationForced =
        Meter.CreateCounter<long>("zlink.termination.forced", "{operation}");

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

    public static void RecordTimerLateness(TimeSpan lateness)
    {
        if (!SpotTimerTickLateness.Enabled) return;
        SafeRecord(SpotTimerTickLateness, Math.Max(0, lateness.TotalSeconds));
    }

    public static void RecordActorCreated() => SafeAdd(ActorCount, 1);
    public static void RecordActorClosed() => SafeAdd(ActorCount, -1);
    internal static bool RelocationInterruptionEnabled =>
        RelocationInterruption.Enabled;

    internal static void RecordRelocationInterruption(
        TimeSpan duration,
        string unitKind,
        string? executionMode)
    {
        if (!RelocationInterruption.Enabled) return;
        if (executionMode is null)
            SafeRecord(
                RelocationInterruption,
                Math.Max(0, duration.TotalSeconds),
                "unit_kind",
                unitKind);
        else
            SafeRecord(
                RelocationInterruption,
                Math.Max(0, duration.TotalSeconds),
                "unit_kind",
                unitKind,
                "execution_mode",
                executionMode);
    }

    public static ZLinkRelocationMetricOperation CreateRelocation(
        string meshName,
        ZLinkRelocationMetricObjectKind objectKind,
        ZLinkRelocationMetricPolicy policy)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(meshName);
        if (!RelocationStarted.Enabled
            && !RelocationCompleted.Enabled
            && !RelocationDuration.Enabled
            && !RelocationRecovered.Enabled)
            return ZLinkRelocationMetricOperation.Disabled;

        return new ZLinkRelocationMetricOperation(
            meshName,
            RelocationObjectKind(objectKind),
            RelocationPolicy(policy));
    }

    public static ZLinkRelocationMetricOperation StartRelocation(
        string meshName,
        ZLinkRelocationMetricObjectKind objectKind,
        ZLinkRelocationMetricPolicy policy)
    {
        var operation = CreateRelocation(meshName, objectKind, policy);
        operation.Start();
        return operation;
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

    public static IDisposable RegisterTerminationState(Func<string> snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        var owner = new object();
        TerminationStateProviders[owner] = snapshot;
        return new ProviderRegistration(() => TerminationStateProviders.TryRemove(owner, out _));
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

    public static void RecordDrainForced(string kind, long count = 1)
    {
        if (count > 0) SafeAdd(DrainForced, count, "kind", kind);
    }

    public static long StartTermination() => StartTimestamp(TerminationDuration);

    public static void CompleteTermination(
        long startedTimestamp,
        string intent,
        string outcome,
        string reason)
    {
        if (startedTimestamp != 0 && TerminationDuration.Enabled)
            SafeRecord(
                TerminationDuration,
                Stopwatch.GetElapsedTime(startedTimestamp).TotalSeconds,
                "intent",
                intent,
                "outcome",
                outcome);
        if (string.Equals(outcome, "blocked", StringComparison.Ordinal))
            SafeAdd(TerminationBlocked, 1, "reason", reason);
        else if (string.Equals(outcome, "force_stopped", StringComparison.Ordinal))
            SafeAdd(
                TerminationForced,
                1,
                "intent",
                intent,
                "reason",
                reason);
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

    private static IEnumerable<Measurement<long>> ObserveTerminationStates()
    {
        foreach (var snapshot in TerminationStateProviders.Values)
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

    private static void SafeAdd(Counter<long> counter, long value, in TagList tags)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(value, tags);
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

    private static void SafeAdd(
        Counter<long> counter,
        long value,
        string firstTagName,
        object? firstTagValue,
        string secondTagName,
        object? secondTagValue)
    {
        if (!counter.Enabled) return;
        try
        {
            counter.Add(
                value,
                new KeyValuePair<string, object?>(firstTagName, firstTagValue),
                new KeyValuePair<string, object?>(secondTagName, secondTagValue));
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

    private static void SafeRecord(
        Histogram<double> histogram,
        double value,
        string firstTagName,
        object? firstTagValue,
        string secondTagName,
        object? secondTagValue)
    {
        if (!histogram.Enabled) return;
        try
        {
            histogram.Record(
                value,
                new KeyValuePair<string, object?>(firstTagName, firstTagValue),
                new KeyValuePair<string, object?>(secondTagName, secondTagValue));
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

    private static void SafeRecord(Histogram<long> histogram, long value, in TagList tags)
    {
        if (!histogram.Enabled) return;
        try
        {
            histogram.Record(value, tags);
        }
        catch
        {
        }
    }

    private static void CompleteRelocation(
        ZLinkRelocationMetricOperation operation,
        ZLinkRelocationMetricOutcome outcome)
    {
        var outcomeValue = RelocationOutcome(outcome);
        var terminalTags = operation.TerminalTags(outcomeValue);
        SafeAdd(RelocationCompleted, 1, terminalTags);
        if (operation.StartedTimestamp != 0 && RelocationDuration.Enabled)
            try
            {
                RelocationDuration.Record(
                    Stopwatch.GetElapsedTime(operation.StartedTimestamp).TotalSeconds,
                    terminalTags);
            }
            catch
            {
            }

        if (outcome == ZLinkRelocationMetricOutcome.Recovered)
            SafeAdd(RelocationRecovered, 1, operation.ObjectTags);
    }

    private static string RelocationObjectKind(ZLinkRelocationMetricObjectKind objectKind) =>
        objectKind switch
        {
            ZLinkRelocationMetricObjectKind.Actor => "actor",
            ZLinkRelocationMetricObjectKind.UserSpot => "user_spot",
            ZLinkRelocationMetricObjectKind.InstanceSpot => "instance_spot",
            _ => throw new ArgumentOutOfRangeException(nameof(objectKind))
        };

    private static string RelocationPolicy(ZLinkRelocationMetricPolicy policy) =>
        policy switch
        {
            ZLinkRelocationMetricPolicy.Recreate => "recreate",
            ZLinkRelocationMetricPolicy.Snapshot => "snapshot",
            _ => throw new ArgumentOutOfRangeException(nameof(policy))
        };

    private static string RelocationOutcome(ZLinkRelocationMetricOutcome outcome) =>
        outcome switch
        {
            ZLinkRelocationMetricOutcome.Completed => "completed",
            ZLinkRelocationMetricOutcome.Aborted => "aborted",
            ZLinkRelocationMetricOutcome.Recovered => "recovered",
            ZLinkRelocationMetricOutcome.Failed => "failed",
            ZLinkRelocationMetricOutcome.Shutdown => "shutdown",
            _ => throw new ArgumentOutOfRangeException(nameof(outcome))
        };

    internal sealed class ZLinkRelocationMetricOperation
    {
        internal static readonly ZLinkRelocationMetricOperation Disabled = new();

        private const int Created = 0;
        private const int Starting = 1;
        private const int Started = 2;
        private const int Completed = 3;

        private int _state;
        private long _startedTimestamp;

        private ZLinkRelocationMetricOperation()
        {
            MeshName = string.Empty;
            ObjectKind = string.Empty;
            Policy = string.Empty;
        }

        internal ZLinkRelocationMetricOperation(
            string meshName,
            string objectKind,
            string policy)
        {
            MeshName = meshName;
            ObjectKind = objectKind;
            Policy = policy;
        }

        private string MeshName { get; }
        private string ObjectKind { get; }
        private string Policy { get; }
        internal long StartedTimestamp => _startedTimestamp;

        internal TagList StartTags =>
            new()
            {
                { "mesh_name", MeshName },
                { "object_kind", ObjectKind },
                { "policy", Policy }
            };

        internal TagList ObjectTags =>
            new()
            {
                { "mesh_name", MeshName },
                { "object_kind", ObjectKind }
            };

        internal TagList TerminalTags(string outcome) =>
            new()
            {
                { "mesh_name", MeshName },
                { "object_kind", ObjectKind },
                { "policy", Policy },
                { "outcome", outcome }
            };

        internal void Complete(ZLinkRelocationMetricOutcome outcome)
        {
            if (ReferenceEquals(this, Disabled))
                return;
            var spinner = new SpinWait();
            while (Volatile.Read(ref _state) == Starting)
                spinner.SpinOnce();
            if (Interlocked.CompareExchange(ref _state, Completed, Started) != Started)
                return;
            CompleteRelocation(this, outcome);
        }

        internal void RecordJournalMessages(long messageCount)
        {
            if (messageCount < 0)
                throw new ArgumentOutOfRangeException(nameof(messageCount));
            if (Volatile.Read(ref _state) != Started
                || !RelocationJournalMessages.Enabled)
                return;
            SafeRecord(RelocationJournalMessages, messageCount, ObjectTags);
        }

        internal void RecordBytes(long byteCount)
        {
            if (byteCount < 0)
                throw new ArgumentOutOfRangeException(nameof(byteCount));
            if (Volatile.Read(ref _state) != Started || !RelocationBytes.Enabled)
                return;
            SafeRecord(RelocationBytes, byteCount, StartTags);
        }

        internal void Start()
        {
            if (ReferenceEquals(this, Disabled)
                || Interlocked.CompareExchange(ref _state, Starting, Created) != Created)
                return;
            _startedTimestamp = RelocationDuration.Enabled
                ? Stopwatch.GetTimestamp()
                : 0;
            SafeAdd(RelocationStarted, 1, StartTags);
            Volatile.Write(ref _state, Started);
        }
    }
}

internal enum ZLinkRelocationMetricObjectKind
{
    Actor,
    UserSpot,
    InstanceSpot
}

internal enum ZLinkRelocationMetricPolicy
{
    Recreate,
    Snapshot
}

internal enum ZLinkRelocationMetricOutcome
{
    Completed,
    Aborted,
    Recovered,
    Failed,
    Shutdown
}
