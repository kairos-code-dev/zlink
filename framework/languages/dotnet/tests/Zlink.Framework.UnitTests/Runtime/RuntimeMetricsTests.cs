using System.Diagnostics.Metrics;
using Microsoft.Extensions.Logging;

namespace Zlink.Framework.UnitTests;

[Collection(RuntimeMetricsCollection.Name)]
public sealed class RuntimeMetricsTests
{
    [Fact]
    public void Meter_Catalog_Uses_Exact_Names_Kinds_Units_And_Scope()
    {
        var instruments = new List<Instrument>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, _) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework)
                    instruments.Add(instrument);
            }
        };

        listener.Start();
        ZLinkRuntimeMetrics.RecordActorCreated();

        var expected = new Dictionary<string, (Type GenericType, string? Unit)>(StringComparer.Ordinal)
        {
            ["zlink.stream.connections.active"] = (typeof(UpDownCounter<>), "{connection}"),
            ["zlink.stream.connections.opened"] = (typeof(Counter<>), "{connection}"),
            ["zlink.stream.connections.closed"] = (typeof(Counter<>), "{connection}"),
            ["zlink.stream.session.bind.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.stream.inbound.bytes"] = (typeof(Counter<>), "By"),
            ["zlink.stream.outbound.bytes"] = (typeof(Counter<>), "By"),
            ["zlink.spot.count"] = (typeof(UpDownCounter<>), "{spot}"),
            ["zlink.spot.timer.tick.lateness"] = (typeof(Histogram<>), "s"),
            ["zlink.spot.created"] = (typeof(Counter<>), "{spot}"),
            ["zlink.spot.closed"] = (typeof(Counter<>), "{spot}"),
            ["zlink.actor.count"] = (typeof(UpDownCounter<>), "{actor}"),
            ["zlink.relocation.started"] = (typeof(Counter<>), "{relocation}"),
            ["zlink.relocation.completed"] = (typeof(Counter<>), "{relocation}"),
            ["zlink.relocation.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.relocation.recovered"] = (typeof(Counter<>), "{relocation}"),
            ["zlink.relocation.journal.messages"] = (typeof(Histogram<>), "{message}"),
            ["zlink.relocation.bytes"] = (typeof(Histogram<>), "By"),
            ["zlink.relocation.interruption"] =
                (typeof(Histogram<>), "s"),
            ["zlink.channel.request.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.channel.request.inflight"] = (typeof(UpDownCounter<>), "{request}"),
            ["zlink.channel.request.timeouts"] = (typeof(Counter<>), "{request}"),
            ["zlink.channel.messages.dropped"] = (typeof(Counter<>), "{message}"),
            ["zlink.fanout.published"] = (typeof(Counter<>), "{message}"),
            ["zlink.fanout.received"] = (typeof(Counter<>), "{message}"),
            ["zlink.location.peers"] = (typeof(ObservableGauge<>), "{peer}"),
            ["zlink.location.store.errors"] = (typeof(Counter<>), "{error}"),
            ["zlink.location.owner_lease.renew.failures"] = (typeof(Counter<>), "{failure}"),
            ["zlink.location.owner_lease.renew.lateness"] = (typeof(Histogram<>), "s"),
            ["zlink.location.write.conflicts"] = (typeof(Counter<>), "{write}"),
            ["zlink.observability.observer.overflow"] = (typeof(Counter<>), "{event}"),
            ["zlink.drain.state"] = (typeof(ObservableGauge<>), null),
            ["zlink.drain.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.drain.actors.handed_off"] = (typeof(Counter<>), "{actor}"),
            ["zlink.drain.forced"] = (typeof(Counter<>), "{item}")
        };

        foreach (var (name, contract) in expected)
        {
            Assert.Contains(
                instruments,
                instrument => instrument.Name == name
                              && instrument.GetType().GetGenericTypeDefinition() == contract.GenericType
                              && instrument.Unit == contract.Unit);
        }

        Assert.DoesNotContain(instruments, instrument => instrument.Name == "zlink.fanout.dropped");
        Assert.DoesNotContain(
            instruments,
            instrument => instrument.Name is
                "zlink.spot.queue.depth"
                or "zlink.spot.queue.wait.duration"
                or "zlink.actor.mailbox.depth");
        Assert.DoesNotContain(
            instruments,
            instrument => instrument.Name.StartsWith(
                "zlink.actor.transfer",
                StringComparison.Ordinal));
        Assert.DoesNotContain(
            instruments,
            instrument => instrument.Name == "zlink.actor.transfers");
    }

    [Fact]
    public void Listener_Failure_Does_Not_Change_Runtime_Result()
    {
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.actor.count")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>(static (_, _, _, _) =>
            throw new InvalidOperationException("application listener failure"));
        listener.Start();

        var exception = Record.Exception(ZLinkRuntimeMetrics.RecordActorCreated);

        Assert.Null(exception);
    }

    [Fact]
    public void Inactive_Histogram_Does_Not_Capture_A_Timestamp()
    {
        Assert.Equal(0, ZLinkRuntimeMetrics.StartStreamSessionBind());
    }

    [Fact]
    public void Relocation_Interruption_Start_Matches_Observation_State()
    {
        var observer = new ZLinkRelocationInterruptionObserver(
            loggerFactory: null);

        var operation = observer.Start(
            ZLinkRelocationUnitKind.Actor,
            "entry");

        Assert.Equal(
            ZLinkRuntimeMetrics.RelocationInterruptionEnabled,
            operation.Enabled);
        operation.Complete();
    }

    [Fact]
    public void Relocation_Interruption_Records_Unit_And_Execution_Mode()
    {
        var measurements =
            new List<(double Value, KeyValuePair<string, object?>[] Tags)>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name
                    == "zlink.relocation.interruption")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<double>(
            (_, value, tags, _) =>
                measurements.Add((value, tags.ToArray())));
        listener.Start();
        var time = new ManualTimeProvider();
        var observer = new ZLinkRelocationInterruptionObserver(
            loggerFactory: null,
            time);

        var operation = observer.Start(
            ZLinkRelocationUnitKind.Actor,
            "entry");
        time.Advance(TimeSpan.FromMilliseconds(750));
        operation.Complete();
        operation.Complete();

        var measurement = Assert.Single(measurements);
        Assert.Equal(0.75, measurement.Value, precision: 3);
        Assert.Contains(
            measurement.Tags,
            tag => tag.Key == "unit_kind"
                   && Equals(tag.Value, "actor"));
        Assert.Contains(
            measurement.Tags,
            tag => tag.Key == "execution_mode"
                   && Equals(tag.Value, "entry"));
    }

    [Fact]
    public void Interruption_Target_Warning_Does_Not_Include_Object_Identifiers()
    {
        var logs = new RecordingLoggerProvider();
        using var loggerFactory = LoggerFactory.Create(builder =>
            builder.SetMinimumLevel(LogLevel.Warning).AddProvider(logs));
        var time = new ManualTimeProvider();
        var observer = new ZLinkRelocationInterruptionObserver(
            loggerFactory,
            time);

        var operation = observer.Start(
            ZLinkRelocationUnitKind.UserSpot,
            "per_actor");
        time.Advance(TimeSpan.FromMilliseconds(1_250));
        operation.Complete();

        var log = Assert.Single(logs.Entries);
        Assert.Equal(LogLevel.Warning, log.Level);
        Assert.Equal("zlink.runtime.relocation.changed", log.EventId.Name);
        Assert.Equal(
            "user_spot",
            log.State["UnitKind"]);
        Assert.Equal(
            "per_actor",
            log.State["ExecutionMode"]);
        Assert.Equal(true, log.State["InterruptionTargetExceeded"]);
        Assert.Equal(
            1.25,
            Assert.IsType<double>(log.State["DurationSeconds"]),
            precision: 3);
        Assert.DoesNotContain(
            log.State.Keys,
            key => key.Contains("ActorId", StringComparison.Ordinal)
                   || key.Contains("SpotId", StringComparison.Ordinal));
    }

    [Fact]
    public void Interruption_Warning_Provider_Failure_Does_Not_Escape()
    {
        using var loggerFactory = LoggerFactory.Create(builder =>
            builder.SetMinimumLevel(LogLevel.Warning)
                .AddProvider(new ThrowingLoggerProvider(
                    throwFromIsEnabled: false)));
        var time = new ManualTimeProvider();
        var observer = new ZLinkRelocationInterruptionObserver(
            loggerFactory,
            time);
        var operation = observer.Start(
            ZLinkRelocationUnitKind.Actor,
            "entry");
        time.Advance(TimeSpan.FromMilliseconds(1_250));

        var exception = Record.Exception(operation.Complete);

        Assert.Null(exception);
    }

    [Fact]
    public void Interruption_Warning_IsEnabled_Failure_Does_Not_Escape()
    {
        using var loggerFactory = LoggerFactory.Create(builder =>
            builder.SetMinimumLevel(LogLevel.Warning)
                .AddProvider(new ThrowingLoggerProvider(
                    throwFromIsEnabled: true)));
        var observer = new ZLinkRelocationInterruptionObserver(
            loggerFactory);

        var exception = Record.Exception(() =>
        {
            var operation = observer.Start(
                ZLinkRelocationUnitKind.Actor,
                "entry");
            operation.Complete();
        });

        Assert.Null(exception);
    }

    [Fact]
    public void Interruption_Logger_Creation_Failure_Does_Not_Escape()
    {
        var exception = Record.Exception(() =>
        {
            var observer = new ZLinkRelocationInterruptionObserver(
                new ThrowingLoggerFactory());
            observer.Start(
                    ZLinkRelocationUnitKind.Actor,
                    "entry")
                .Complete();
        });

        Assert.Null(exception);
    }

    [Fact]
    public void Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State()
    {
        ZLinkRuntimeMetrics.RecordLocationStoreError();
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();

        for (var index = 0; index < 1_000_000; index++)
            ZLinkRuntimeMetrics.RecordLocationStoreError();

        Assert.Equal(allocatedBefore, GC.GetAllocatedBytesForCurrentThread());
    }

    [Fact]
    public async Task Actor_Transfer_Pending_Count_Excludes_One_Way_Send()
    {
        var mailbox = new ZLinkActorDispatchMailbox();
        var active = await mailbox.EnterAsync(CancellationToken.None);
        var pendingSend = mailbox.EnterAsync(
                CancellationToken.None,
                countAsPendingMessage: true)
            .AsTask();
        var pendingRequest = mailbox.EnterAsync(
                CancellationToken.None,
                countAsPendingMessage: true,
                countAsPendingRequest: true)
            .AsTask();

        Assert.Equal(1, mailbox.PendingRequestCount);

        active.Dispose();
        (await pendingSend).Dispose();
        (await pendingRequest).Dispose();
        Assert.Equal(0, mailbox.PendingRequestCount);
    }

    [Fact]
    public void Relocation_Metric_Uses_Closed_Labels_And_Records_Terminal_Once()
    {
        var counterSamples = new List<(string Name, IReadOnlyDictionary<string, string> Tags)>();
        var durationSamples =
            new List<(double Value, IReadOnlyDictionary<string, string> Tags)>();
        using var counterListener = Listen<long>(
            ["zlink.relocation.started", "zlink.relocation.completed"],
            (instrument, _, tags) => counterSamples.Add((instrument.Name, Tags(tags))));
        using var durationListener = Listen<double>(
            "zlink.relocation.duration",
            (_, value, tags) => durationSamples.Add((value, Tags(tags))));

        var operation = ZLinkRuntimeMetrics.StartRelocation(
            "game",
            ZLinkRelocationMetricObjectKind.Actor,
            ZLinkRelocationMetricPolicy.Snapshot);
        operation.Complete(ZLinkRelocationMetricOutcome.Aborted);
        operation.Complete(ZLinkRelocationMetricOutcome.Completed);

        var started = Assert.Single(
            counterSamples,
            sample => sample.Name == "zlink.relocation.started");
        Assert.Equal(
            new Dictionary<string, string>
            {
                ["mesh_name"] = "game",
                ["object_kind"] = "actor",
                ["policy"] = "snapshot"
            },
            started.Tags);
        var completed = Assert.Single(
            counterSamples,
            sample => sample.Name == "zlink.relocation.completed");
        Assert.Equal("game", completed.Tags["mesh_name"]);
        Assert.Equal("actor", completed.Tags["object_kind"]);
        Assert.Equal("snapshot", completed.Tags["policy"]);
        Assert.Equal("aborted", completed.Tags["outcome"]);
        var duration = Assert.Single(durationSamples);
        Assert.True(duration.Value >= 0);
        Assert.Equal(completed.Tags, duration.Tags);
    }

    [Fact]
    public void Relocation_Metric_Uses_Only_The_Closed_Terminal_Outcomes()
    {
        var terminal = new List<IReadOnlyDictionary<string, string>>();
        var recovered = new List<IReadOnlyDictionary<string, string>>();
        using var listener = Listen<long>(
            ["zlink.relocation.completed", "zlink.relocation.recovered"],
            (instrument, _, tags) =>
            {
                if (instrument.Name == "zlink.relocation.completed")
                    terminal.Add(Tags(tags));
                else
                    recovered.Add(Tags(tags));
            });

        var outcomes = new[]
        {
            ZLinkRelocationMetricOutcome.Completed,
            ZLinkRelocationMetricOutcome.Aborted,
            ZLinkRelocationMetricOutcome.Recovered,
            ZLinkRelocationMetricOutcome.Failed,
            ZLinkRelocationMetricOutcome.Shutdown
        };
        foreach (var outcome in outcomes)
            ZLinkRuntimeMetrics.StartRelocation(
                    "mesh",
                    ZLinkRelocationMetricObjectKind.UserSpot,
                    ZLinkRelocationMetricPolicy.Recreate)
                .Complete(outcome);

        Assert.Equal(
            ["completed", "aborted", "recovered", "failed", "shutdown"],
            terminal.Select(static sample => sample["outcome"]));
        var recoveryTags = Assert.Single(recovered);
        Assert.Equal("mesh", recoveryTags["mesh_name"]);
        Assert.Equal("user_spot", recoveryTags["object_kind"]);
        Assert.DoesNotContain("policy", recoveryTags);
        Assert.DoesNotContain("outcome", recoveryTags);
    }

    [Fact]
    public void Relocation_Metric_Records_One_Terminal_When_Outcomes_Compete()
    {
        var terminalCount = 0;
        var durationCount = 0;
        using var terminalListener = Listen<long>(
            "zlink.relocation.completed",
            (_, _, _) => Interlocked.Increment(ref terminalCount));
        using var durationListener = Listen<double>(
            "zlink.relocation.duration",
            (_, _, _) => Interlocked.Increment(ref durationCount));
        var operation = ZLinkRuntimeMetrics.StartRelocation(
            "mesh",
            ZLinkRelocationMetricObjectKind.Actor,
            ZLinkRelocationMetricPolicy.Recreate);
        var outcomes = Enum.GetValues<ZLinkRelocationMetricOutcome>();

        Parallel.For(
            0,
            256,
            index => operation.Complete(outcomes[index % outcomes.Length]));

        Assert.Equal(1, Volatile.Read(ref terminalCount));
        Assert.Equal(1, Volatile.Read(ref durationCount));
    }

    [Fact]
    public void Relocation_Journal_And_Bytes_Use_Their_Exact_Label_Sets()
    {
        var samples = new List<(string Name, long Value, IReadOnlyDictionary<string, string> Tags)>();
        using var listener = Listen<long>(
            ["zlink.relocation.journal.messages", "zlink.relocation.bytes"],
            (instrument, value, tags) => samples.Add((instrument.Name, value, Tags(tags))));

        var operation = ZLinkRuntimeMetrics.StartRelocation(
            "mesh",
            ZLinkRelocationMetricObjectKind.InstanceSpot,
            ZLinkRelocationMetricPolicy.Snapshot);
        operation.RecordJournalMessages(7);
        operation.RecordBytes(4096);
        operation.Complete(ZLinkRelocationMetricOutcome.Completed);

        var journal = Assert.Single(
            samples,
            sample => sample.Name == "zlink.relocation.journal.messages");
        Assert.Equal(7, journal.Value);
        Assert.Equal("mesh", journal.Tags["mesh_name"]);
        Assert.Equal("instance_spot", journal.Tags["object_kind"]);
        Assert.DoesNotContain("policy", journal.Tags);

        var bytes = Assert.Single(
            samples,
            sample => sample.Name == "zlink.relocation.bytes");
        Assert.Equal(4096, bytes.Value);
        Assert.Equal("mesh", bytes.Tags["mesh_name"]);
        Assert.Equal("instance_spot", bytes.Tags["object_kind"]);
        Assert.Equal("snapshot", bytes.Tags["policy"]);
    }

    [Fact]
    public void Channel_Request_Metrics_Close_Inflight_And_Count_Only_Timeouts()
    {
        var inflight = new List<long>();
        var durations = new List<double>();
        var timeouts = new List<long>();
        using var inflightListener = Listen<long>(
            "zlink.channel.request.inflight",
            (_, value, _) => inflight.Add(value));
        using var durationListener = Listen<double>(
            "zlink.channel.request.duration",
            (_, value, _) => durations.Add(value));
        using var timeoutListener = Listen<long>(
            "zlink.channel.request.timeouts",
            (_, value, _) => timeouts.Add(value));

        var completed = ZLinkRuntimeMetrics.StartChannelRequest();
        ZLinkRuntimeMetrics.CompleteChannelRequest(completed, timedOut: false);
        var timedOut = ZLinkRuntimeMetrics.StartChannelRequest();
        ZLinkRuntimeMetrics.CompleteChannelRequest(timedOut, timedOut: true);

        Assert.Equal([1L, -1L, 1L, -1L], inflight);
        Assert.Equal(2, durations.Count);
        Assert.Equal([1L], timeouts);
    }

    [Fact]
    public void Spot_Count_And_Lifecycle_Counters_Keep_Entry_And_User_Separate()
    {
        var samples = new List<(string Name, long Value, string? Kind)>();
        using var listener = Listen<long>(
            ["zlink.spot.count", "zlink.spot.created", "zlink.spot.closed"],
            (instrument, value, tags) => samples.Add((instrument.Name, value, Tag(tags, "kind"))));

        ZLinkRuntimeMetrics.RecordSpotCreated("entry");
        ZLinkRuntimeMetrics.RecordSpotCreated("user");
        ZLinkRuntimeMetrics.RecordSpotClosed("entry");
        ZLinkRuntimeMetrics.RecordSpotClosed("user");

        Assert.Equal(0, samples.Where(sample => sample.Name == "zlink.spot.count" && sample.Kind == "entry").Sum(sample => sample.Value));
        Assert.Equal(0, samples.Where(sample => sample.Name == "zlink.spot.count" && sample.Kind == "user").Sum(sample => sample.Value));
        Assert.Single(samples, sample => sample.Name == "zlink.spot.created" && sample.Kind == "entry");
        Assert.Single(samples, sample => sample.Name == "zlink.spot.created" && sample.Kind == "user");
    }

    [Fact]
    public void Location_And_Dropped_Metrics_Use_Closed_Labels_And_Ignore_Listener_Failure()
    {
        var samples = new List<(string Name, IReadOnlyDictionary<string, string> Tags)>();
        using var listener = Listen<long>(
            [
                "zlink.location.owner_lease.renew.failures",
                "zlink.location.write.conflicts",
                "zlink.channel.messages.dropped"
            ],
            (instrument, _, tags) => samples.Add((instrument.Name, Tags(tags))));

        ZLinkRuntimeMetrics.RecordOwnerLeaseRenewFailure();
        ZLinkRuntimeMetrics.RecordLocationWriteConflict();
        ZLinkRuntimeMetrics.RecordChannelDropped("channel", "request", "stale_route");

        Assert.Contains(samples, sample => sample.Name == "zlink.location.owner_lease.renew.failures");
        Assert.Contains(samples, sample => sample.Name == "zlink.location.write.conflicts");
        var dropped = Assert.Single(samples, sample => sample.Name == "zlink.channel.messages.dropped");
        Assert.Equal("channel", dropped.Tags["surface"]);
        Assert.Equal("request", dropped.Tags["kind"]);
        Assert.Equal("stale_route", dropped.Tags["reason"]);
    }

    [Fact]
    public void Fanout_Without_A_Declared_Topic_Omits_The_Topic_Label()
    {
        var samples = new List<IReadOnlyDictionary<string, string>>();
        using var listener = Listen<long>(
            ["zlink.fanout.published", "zlink.fanout.received"],
            (_, _, tags) => samples.Add(Tags(tags)));

        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        ZLinkRuntimeMetrics.RecordFanoutReceived(null);

        Assert.Equal(2, samples.Count);
        Assert.All(samples, tags => Assert.DoesNotContain("topic", tags));
    }

    [Fact]
    public void Session_Bind_Duration_Records_One_Completed_Interval()
    {
        var samples = new List<double>();
        using var listener = Listen<double>(
            "zlink.stream.session.bind.duration",
            (_, value, _) => samples.Add(value));

        var started = ZLinkRuntimeMetrics.StartStreamSessionBind();
        ZLinkRuntimeMetrics.CompleteStreamSessionBind(started);

        Assert.Single(samples);
        Assert.True(samples[0] >= 0);
    }

    [Fact]
    public void Forced_Drain_Records_The_Exact_Count_For_Each_Closed_Kind()
    {
        var samples = new List<(long Value, string? Kind)>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name == "zlink.drain.forced")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>((instrument, value, tags, _) =>
        {
            if (instrument.Name == "zlink.drain.forced")
                samples.Add((value, Tag(tags, "kind")));
        });
        listener.Start();

        ZLinkRuntimeMetrics.RecordDrainForced("actor", 2);
        ZLinkRuntimeMetrics.RecordDrainForced("spot", 3);
        ZLinkRuntimeMetrics.RecordDrainForced("request", 4);
        ZLinkRuntimeMetrics.RecordDrainForced("session", 5);

        Assert.Equal(
            new[]
            {
                (2L, (string?)"actor"),
                (3L, (string?)"spot"),
                (4L, (string?)"request"),
                (5L, (string?)"session")
            },
            samples);
    }

    [Fact]
    public void Observable_Metrics_Pull_Current_Source_State_After_Listener_Attaches()
    {
        long firstPeers = 2;
        long secondPeers = 3;
        var drainState = "drained";
        using var first = ZLinkRuntimeMetrics.RegisterLocationPeers(() => firstPeers);
        var second = ZLinkRuntimeMetrics.RegisterLocationPeers(() => secondPeers);
        using var drain = ZLinkRuntimeMetrics.RegisterDrainState(() => drainState);
        var peerSamples = new List<long>();
        var drainSamples = new List<string?>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name is "zlink.location.peers" or "zlink.drain.state")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>((instrument, value, tags, _) =>
        {
            if (instrument.Name == "zlink.location.peers") peerSamples.Add(value);
            if (instrument.Name == "zlink.drain.state") drainSamples.Add(Tag(tags, "state"));
        });
        listener.Start();

        listener.RecordObservableInstruments();
        Assert.Contains(5, peerSamples);
        Assert.Contains("drained", drainSamples);

        firstPeers = 4;
        drainState = "force_stopping";
        second.Dispose();
        listener.RecordObservableInstruments();
        Assert.Equal(4, peerSamples[^1]);
        Assert.Equal("force_stopping", drainSamples[^1]);
    }

    private static string? Tag(ReadOnlySpan<KeyValuePair<string, object?>> tags, string name)
    {
        foreach (var tag in tags)
            if (tag.Key == name) return tag.Value as string;
        return null;
    }

    private static IReadOnlyDictionary<string, string> Tags(
        ReadOnlySpan<KeyValuePair<string, object?>> tags)
    {
        var result = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var tag in tags) result[tag.Key] = tag.Value?.ToString() ?? string.Empty;
        return result;
    }

    private static MeterListener Listen<T>(
        string instrumentName,
        MetricRecorder<T> record)
        where T : struct => Listen([instrumentName], record);

    private static MeterListener Listen<T>(
        IReadOnlyCollection<string> instrumentNames,
        MetricRecorder<T> record)
        where T : struct
    {
        var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrumentNames.Contains(instrument.Name))
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<T>((instrument, value, tags, _) => record(instrument, value, tags));
        listener.Start();
        return listener;
    }

    private sealed class RecordingLoggerProvider : ILoggerProvider
    {
        internal List<LogEntry> Entries { get; } = [];

        public ILogger CreateLogger(string categoryName) =>
            new RecordingLogger(Entries);

        public void Dispose()
        {
        }
    }

    private sealed class ThrowingLoggerProvider(bool throwFromIsEnabled)
        : ILoggerProvider
    {
        public ILogger CreateLogger(string categoryName) =>
            new ThrowingLogger(throwFromIsEnabled);

        public void Dispose()
        {
        }
    }

    private sealed class ThrowingLoggerFactory : ILoggerFactory
    {
        public void AddProvider(ILoggerProvider provider)
        {
        }

        public ILogger CreateLogger(string categoryName) =>
            throw new InvalidOperationException(
                "injected CreateLogger failure");

        public void Dispose()
        {
        }
    }

    private sealed class ThrowingLogger(bool throwFromIsEnabled) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state)
            where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) =>
            throwFromIsEnabled
                ? throw new InvalidOperationException(
                    "injected IsEnabled failure")
                : true;

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter) =>
            throw new InvalidOperationException("injected logger failure");
    }

    private sealed class RecordingLogger(List<LogEntry> entries) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state)
            where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) =>
            logLevel >= LogLevel.Warning;

        public void Log<TState>(
            LogLevel logLevel,
            EventId eventId,
            TState state,
            Exception? exception,
            Func<TState, Exception?, string> formatter)
        {
            var values = state is IEnumerable<KeyValuePair<string, object?>>
                structured
                ? structured.ToDictionary(
                    static pair => pair.Key,
                    static pair => pair.Value,
                    StringComparer.Ordinal)
                : [];
            entries.Add(new LogEntry(logLevel, eventId, values));
        }
    }

    private sealed record LogEntry(
        LogLevel Level,
        EventId EventId,
        IReadOnlyDictionary<string, object?> State);

    private delegate void MetricRecorder<T>(
        Instrument instrument,
        T value,
        ReadOnlySpan<KeyValuePair<string, object?>> tags)
        where T : struct;
}

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class RuntimeMetricsCollection
{
    public const string Name = "Runtime metrics isolation";
}
