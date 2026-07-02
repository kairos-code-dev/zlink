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
                ZLinkLocationRuntimeEventKind.StatusChanged,
                ZLinkLocationRuntimeEventKind.TopologyChanged,
                ZLinkLocationRuntimeEventKind.ServiceSummaryChanged
            ],
            events.Select(static @event => @event.Event).ToArray());
        Assert.All(events, static @event => Assert.Equal("locations", @event.SourceName));
        Assert.NotNull(events[0].Status);
        Assert.NotNull(events[1].Topology);
        Assert.NotNull(events[2].ServiceSummary);
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
        Assert.Equal(ZLinkLocationRuntimeEventKind.TopologyChanged, Assert.Single(events).Event);
        events.Clear();

        // Store health flips: only the status section changes.
        diff.DispatchChanges(Snapshot(storeHealthy: false, "tcp://127.0.0.1:9001"), Timestamp, events.Add);
        Assert.Equal(ZLinkLocationRuntimeEventKind.StatusChanged, Assert.Single(events).Event);
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
                OwnerLeaseRenewedAt: null,
                PeerCacheEntryCount: 0,
                SpotCacheEntryCount: 0,
                ActorCacheEntryCount: 0,
                RouteCacheEntryCount: 0),
            [
                new ZLinkLocationTopologyEntry(
                    ZLinkLocationKind.Peer,
                    "play",
                    ZLinkLocationRole.Router,
                    RoutingId.From("node-1"),
                    null,
                    null,
                    endpoint,
                    ZLinkLocationTopologyState.Ready,
                    1,
                    1,
                    0,
                    Timestamp)
            ],
            [
                new ZLinkLocationServiceSummary(
                    "play",
                    ZLinkLocationAutoConnectType.RouteMesh,
                    ZLinkLocationRole.Router,
                    1,
                    1,
                    0,
                    0,
                    Timestamp)
            ]);
}
