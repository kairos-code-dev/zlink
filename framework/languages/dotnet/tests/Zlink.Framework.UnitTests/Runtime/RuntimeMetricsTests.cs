using System.Diagnostics.Metrics;

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
            ["zlink.spot.queue.depth"] = (typeof(UpDownCounter<>), "{item}"),
            ["zlink.spot.queue.wait.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.spot.timer.tick.lateness"] = (typeof(Histogram<>), "s"),
            ["zlink.spot.created"] = (typeof(Counter<>), "{spot}"),
            ["zlink.spot.closed"] = (typeof(Counter<>), "{spot}"),
            ["zlink.actor.count"] = (typeof(UpDownCounter<>), "{actor}"),
            ["zlink.actor.mailbox.depth"] = (typeof(UpDownCounter<>), "{item}"),
            ["zlink.actor.transfers"] = (typeof(Counter<>), "{transfer}"),
            ["zlink.actor.transfer.duration"] = (typeof(Histogram<>), "s"),
            ["zlink.actor.transfer.pending_requests.count"] = (typeof(Histogram<>), "{request}"),
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
            ["zlink.drain.rooms.drained"] = (typeof(Counter<>), "{spot}"),
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
    public void Inactive_Meter_Does_Not_Allocate_Or_Retain_Per_Event_State()
    {
        ZLinkRuntimeMetrics.RecordLocationStoreError();
        var allocatedBefore = GC.GetAllocatedBytesForCurrentThread();

        for (var index = 0; index < 1_000_000; index++)
            ZLinkRuntimeMetrics.RecordLocationStoreError();

        Assert.Equal(allocatedBefore, GC.GetAllocatedBytesForCurrentThread());
    }

    [Fact]
    public async Task Spot_Queue_Records_Depth_Wait_And_Kind()
    {
        var depth = new List<(long Value, string? Kind)>();
        var waits = new List<(double Value, string? Kind)>();
        using var listener = new MeterListener
        {
            InstrumentPublished = (instrument, owner) =>
            {
                if (instrument.Meter.Name == ZLinkMeters.Framework
                    && instrument.Name is "zlink.spot.queue.depth" or "zlink.spot.queue.wait.duration")
                    owner.EnableMeasurementEvents(instrument);
            }
        };
        listener.SetMeasurementEventCallback<long>((instrument, value, tags, _) =>
        {
            if (instrument.Name != "zlink.spot.queue.depth") return;
            lock (depth) depth.Add((value, Tag(tags, "kind")));
        });
        listener.SetMeasurementEventCallback<double>((instrument, value, tags, _) =>
        {
            if (instrument.Name != "zlink.spot.queue.wait.duration") return;
            lock (waits) waits.Add((value, Tag(tags, "kind")));
        });
        listener.Start();

        var errorSink = new ZLinkRuntimeErrorSink();
        await using var queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, CancellationToken.None, new object()),
            errorSink,
            CancellationToken.None,
            spotMetricKind: "user");
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var first = await queue.PostAsync(
            async _ =>
            {
                firstStarted.TrySetResult();
                await releaseFirst.Task.ConfigureAwait(false);
            },
            CancellationToken.None);
        await firstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var secondStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var second = await queue.PostAsync(
            _ =>
            {
                secondStarted.TrySetResult();
                return ValueTask.CompletedTask;
            },
            CancellationToken.None);
        releaseFirst.TrySetResult();
        await Task.WhenAll(first.Completion, second.Completion).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(0, depth.Sum(sample => sample.Value));
        Assert.All(depth, sample => Assert.Equal("user", sample.Kind));
        Assert.Equal(2, waits.Count);
        Assert.All(waits, sample =>
        {
            Assert.True(sample.Value >= 0);
            Assert.Equal("user", sample.Kind);
        });
    }

    [Fact]
    public void Spot_Queue_Wait_Samples_Support_A_Reader_Computed_P99()
    {
        var waits = new List<double>();
        using var listener = Listen<double>("zlink.spot.queue.wait.duration", (_, value, _) => waits.Add(value));

        for (var index = 0; index < 100; index++)
        {
            var started = ZLinkRuntimeMetrics.RecordSpotQueueEnqueued("user");
            ZLinkRuntimeMetrics.RecordSpotQueueStarted("user", started);
        }

        var ordered = waits.Order().ToArray();
        var p99 = ordered[(int)Math.Ceiling(ordered.Length * 0.99d) - 1];
        Assert.Equal(100, ordered.Length);
        Assert.True(p99 >= 0);
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
    public async Task Actor_Transfer_Metric_Uses_The_Moving_Snapshot_Once()
    {
        var pendingSamples = new List<long>();
        var transferSamples = new List<long>();
        var durationSamples = new List<double>();
        using var pendingListener = Listen<long>(
            "zlink.actor.transfer.pending_requests.count",
            (_, value, _) => pendingSamples.Add(value));
        using var transferListener = Listen<long>(
            "zlink.actor.transfers",
            (_, value, _) => transferSamples.Add(value));
        using var durationListener = Listen<double>(
            "zlink.actor.transfer.duration",
            (_, value, _) => durationSamples.Add(value));
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

        var movingSnapshot = mailbox.PendingRequestCount;
        var started = ZLinkRuntimeMetrics.StartActorTransfer(movingSnapshot);
        ZLinkRuntimeMetrics.CompleteActorTransfer(started);

        Assert.Equal([1L], pendingSamples);
        Assert.Equal([1L], transferSamples);
        Assert.Single(durationSamples);
        active.Dispose();
        (await pendingSend).Dispose();
        (await pendingRequest).Dispose();
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
