using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class OwnerLeaseTrackerTests
{
    [Fact]
    public async Task Lease_Expiry_Uses_Store_Time_Plus_Monotonic_Elapsed_Never_The_Wall_Clock()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync("owner-a", RoutingId.From("node-1"), TimeSpan.FromSeconds(15));

        // A long polling interval keeps the snapshot cached, so expiry is
        // judged from the snapshot's StoreNow plus local monotonic elapsed.
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMinutes(10) };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        Assert.True(await tracker.IsOwnerLiveAsync("owner-a"));

        // An application wall-clock jump alone never expires an owner:
        // wall clocks are not part of the expiry computation (draft 6.6).
        time.AdvanceWallClockOnly(TimeSpan.FromHours(1));
        Assert.True(await tracker.IsOwnerLiveAsync("owner-a"));

        // Monotonic elapsed time past the TTL does.
        time.AdvanceMonotonicOnly(TimeSpan.FromSeconds(16));
        Assert.False(await tracker.IsOwnerLiveAsync("owner-a"));
    }

    [Fact]
    public async Task Snapshot_Staleness_Is_Bounded_By_The_Polling_Interval()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync("owner-a", RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.FromSeconds(1) };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        Assert.True(await tracker.IsOwnerLiveAsync("owner-a"));

        // The owner disappears from the store; the cached snapshot may keep
        // it alive for at most one polling interval.
        await store.RemoveOwnerLeaseAsync("owner-a");
        Assert.True(await tracker.IsOwnerLiveAsync("owner-a"));

        time.Advance(TimeSpan.FromSeconds(1));
        Assert.False(await tracker.IsOwnerLiveAsync("owner-a"));
    }
}
