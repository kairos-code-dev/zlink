using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class LocationRuntimePollingDiffTests
{
    [Fact]
    public void First_Snapshot_Publishes_Every_Section()
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff("locations");
        var events = new List<ZLinkLocationRuntimeEvent>();

        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);

        Assert.Equal(
            [
                typeof(ZLinkLocationRuntimeEvent.StatusChanged),
                typeof(ZLinkLocationRuntimeEvent.TopologyChanged),
                typeof(ZLinkLocationRuntimeEvent.ServiceSummaryChanged)
            ],
            events.Select(static @event => @event.GetType()).ToArray());
        Assert.All(events, static @event => Assert.Equal("locations", @event.SourceName));
        Assert.IsType<ZLinkLocationRuntimeEvent.StatusChanged>(events[0]);
        Assert.IsType<ZLinkLocationRuntimeEvent.TopologyChanged>(events[1]);
        Assert.IsType<ZLinkLocationRuntimeEvent.ServiceSummaryChanged>(events[2]);
    }

    [Fact]
    public void Unchanged_Snapshot_Publishes_Nothing()
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff("locations");
        var events = new List<ZLinkLocationRuntimeEvent>();

        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        events.Clear();

        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);

        Assert.Empty(events);
    }

    [Fact]
    public void Only_The_Changed_Sections_Are_Published()
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff("locations");
        var events = new List<ZLinkLocationRuntimeEvent>();
        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        events.Clear();

        // A topology endpoint changes while status and summaries stay put.
        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9001"), Timestamp, events.Add);
        Assert.IsType<ZLinkLocationRuntimeEvent.TopologyChanged>(Assert.Single(events));
    }

    [Fact]
    public void Store_Health_Transitions_Publish_Unavailable_And_Recovered()
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff("locations");
        var events = new List<ZLinkLocationRuntimeEvent>();
        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        events.Clear();

        // Health flips down: the status change and the outage transition.
        diff.DispatchChanges(Snapshot(storeHealthy: false, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        Assert.Equal(
            [
                typeof(ZLinkLocationRuntimeEvent.StatusChanged),
                typeof(ZLinkLocationRuntimeEvent.StoreFailure)
            ],
            events.Select(static @event => @event.GetType()).ToArray());
        events.Clear();

        // Still down: no repeated transition event.
        diff.DispatchChanges(Snapshot(storeHealthy: false, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        Assert.Empty(events);

        // Health flips back up: the recovery transition.
        diff.DispatchChanges(Snapshot(storeHealthy: true, "tcp://127.0.0.1:9000"), Timestamp, events.Add);
        Assert.Equal(
            [
                typeof(ZLinkLocationRuntimeEvent.StatusChanged),
                typeof(ZLinkLocationRuntimeEvent.StoreRecovered)
            ],
            events.Select(static @event => @event.GetType()).ToArray());
    }

    [Fact]
    public void First_Snapshot_With_Unhealthy_Store_Publishes_StoreFailure()
    {
        var diff = new ZLinkLocationRuntimePollingEventDiff("locations");
        var events = new List<ZLinkLocationRuntimeEvent>();

        diff.DispatchChanges(Snapshot(storeHealthy: false, "tcp://127.0.0.1:9000"), Timestamp, events.Add);

        Assert.Contains(events, static @event => @event is ZLinkLocationRuntimeEvent.StoreFailure);
    }

    private static readonly DateTimeOffset Timestamp = new(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);

    private static ZLinkLocationRuntimePollingSnapshot Snapshot(bool storeHealthy, string endpoint) =>
        new(
            new ZLinkLocationRuntimeStatus(
                StoreHealthy: storeHealthy,
                WatchEnabled: false,
                PollingInterval: TimeSpan.FromSeconds(1),
                LastRefreshAt: null,
                LastError: null,
                OwnerLeaseHealthy: storeHealthy,
                OwnerLeaseRenewedAt: null),
            [
                new ZLinkLocationTopologyEntry(
                    "play",
                    RoutingId.From("node-1"),
                    endpoint,
                    Draining: false,
                    ZLinkLocationTopologyState.Ready,
                    Timestamp)
            ],
            [
                new ZLinkLocationServiceSummary(
                    "play",
                    1,
                    1,
                    0,
                    0,
                    Timestamp)
            ]);
}
