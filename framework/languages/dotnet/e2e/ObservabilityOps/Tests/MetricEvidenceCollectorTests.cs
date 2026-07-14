using System.Diagnostics.Metrics;
using ObservabilityOps.Server.Support;
using ObservabilityOps.Shared;
using Xunit;
using Zlink.Framework.Contracts.Eventing;

public sealed class MetricEvidenceCollectorTests
{
    [Fact]
    public void Snapshot_Preserves_Instrument_Semantics_And_Replaces_Observable_Series()
    {
        using var collector = new MetricEvidenceCollector();
        using var meter = new Meter(ZLinkMeters.Framework);
        var counter = meter.CreateCounter<long>("test.counter");
        var active = meter.CreateUpDownCounter<long>("test.active");
        var duration = meter.CreateHistogram<double>("test.duration");
        var observableState = "serving";
        _ = meter.CreateObservableGauge(
            "test.state",
            () => new Measurement<long>(
                1,
                new KeyValuePair<string, object?>("state", observableState)));

        counter.Add(2);
        counter.Add(3);
        active.Add(2);
        active.Add(-1);
        duration.Record(2);
        duration.Record(4);
        var first = collector.Snapshot();
        Assert.Equal(5m, Single(first, "test.counter").Value);
        Assert.Equal(1m, Single(first, "test.active").Value);
        var histogram = Single(first, "test.duration");
        Assert.Equal(2, histogram.Count);
        Assert.Equal(6m, histogram.Sum);
        Assert.Equal(2m, histogram.Min);
        Assert.Equal(4m, histogram.Max);
        Assert.Equal("serving", Single(first, "test.state").Tags["state"]);

        observableState = "draining";
        var second = collector.Snapshot();
        var stateSeries = second.Where(sample => sample.Name == "test.state").ToArray();
        Assert.Single(stateSeries);
        Assert.Equal("draining", stateSeries[0].Tags["state"]);
    }

    [Fact]
    public void Snapshot_Aggregates_Providers_And_Preserves_Long_Precision()
    {
        using var collector = new MetricEvidenceCollector();
        using var firstMeter = new Meter(ZLinkMeters.Framework);
        using var secondMeter = new Meter(ZLinkMeters.Framework);
        _ = firstMeter.CreateObservableGauge("test.aggregate", () => 2L);
        _ = secondMeter.CreateObservableGauge("test.aggregate", () => 3L);
        var exact = firstMeter.CreateCounter<long>("test.exact-long");
        exact.Add(9_007_199_254_740_993L);

        var snapshot = collector.Snapshot();

        Assert.Equal(5m, Single(snapshot, "test.aggregate").Value);
        Assert.Equal(9_007_199_254_740_993m, Single(snapshot, "test.exact-long").Value);
    }

    private static MetricSample Single(IEnumerable<MetricSample> samples, string name) =>
        Assert.Single(samples.Where(sample => sample.Name == name));
}
