using System.Diagnostics.Metrics;

namespace Zlink.Framework.UnitTests;

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
            Assert.Null(sample.Kind);
        });
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
}
