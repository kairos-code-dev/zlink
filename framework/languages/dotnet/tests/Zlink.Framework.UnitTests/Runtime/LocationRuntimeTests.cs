using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class LocationRuntimeTests
{
    [Fact]
    public async Task Owner_Lease_Renew_Tracks_Health_And_Recovers_After_Store_Failure()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var flaky = new FlakyOwnerLeaseStore(store);
        var runtime = NewRuntime(store, flaky, time);

        Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        Assert.Null(runtime.LastError);

        // Fail-static: a store outage is recorded, never thrown, and the
        // next successful renew clears the error.
        flaky.Fail = true;
        Assert.False(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.False(runtime.GetHealthSnapshot().Healthy);
        Assert.NotNull(runtime.LastError);

        flaky.Fail = false;
        Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        Assert.Null(runtime.LastError);
    }

    [Fact]
    public async Task Claim_Then_Activate_Race_Gives_One_Winner_Across_Runtimes()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtimeA = NewRuntime(store, store, time);
        var runtimeB = NewRuntime(store, store, time);
        await runtimeA.RenewOwnerLeaseOnceAsync();
        await runtimeB.RenewOwnerLeaseOnceAsync();

        var actor = InMemoryLocationStoreTests.Actor("ignored", 0);

        // The runtime stamps its own owner id: writers never choose owners.
        var winner = await runtimeA.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var loser = await runtimeB.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(ZLinkLocationWriteStatus.Stored, winner.Status);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, loser.Status);

        var row = await store.ResolveActorAsync(new ZLinkActorLocationKey("actor-1"));
        Assert.Equal(runtimeA.OwnerId, row!.OwnerId);
    }

    [Fact]
    public async Task Takeover_Makes_Old_Owner_Writes_Stale_And_Raises_OwnershipLost()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var oldOwner = NewRuntime(store, store, time);
        var newOwner = NewRuntime(store, store, time);
        await oldOwner.RenewOwnerLeaseOnceAsync();
        await newOwner.RenewOwnerLeaseOnceAsync();

        var lost = new List<(ZLinkLocationKind Kind, string Key)>();
        oldOwner.OwnershipLost += (kind, key) => lost.Add((kind, key));

        var actor = InMemoryLocationStoreTests.Actor("ignored", 0);
        var claimed = await oldOwner.WriteActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        await newOwner.WriteActorAsync(actor, ZLinkLocationWriteIntent.Takeover);

        // The old owner notices the silent takeover on its next row write
        // and must deactivate its local instance.
        var stale = await oldOwner.WriteActorAsync(
            actor with { Generation = claimed.Generation }, ZLinkLocationWriteIntent.Renew);

        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale.Status);
        var single = Assert.Single(lost);
        Assert.Equal(ZLinkLocationKind.Actor, single.Kind);
    }

    [Fact]
    public async Task Shutdown_Removes_Owner_Lease_Then_Bulk_Removes_Rows()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtime = NewRuntime(store, store, time);
        await runtime.StartAsync(RoutingId.From("node-1"));
        await runtime.WriteActorAsync(
            InMemoryLocationStoreTests.Actor("ignored", 0), ZLinkLocationWriteIntent.NewClaim);
        await runtime.WriteSpotAsync(
            InMemoryLocationStoreTests.Spot("ignored", "spot-1"), ZLinkLocationWriteIntent.NewClaim);
        await runtime.WritePeerAsync(
            InMemoryLocationStoreTests.Peer("ignored"), ZLinkLocationWriteIntent.NewClaim);

        await runtime.StopAsync();

        var snapshot = await store.ListOwnerLeasesAsync();
        Assert.Empty(snapshot.Leases);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("actor-1")));
        Assert.Null(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("play", RoutingId.From("spot-1"))));
        Assert.Empty(await store.ListPeersAsync(new ZLinkPeerLocationFilter(MeshName: "play")));
    }

    [Fact]
    public async Task Drain_Cleanup_Failure_Keeps_Lease_Heartbeat_Until_Retry_Succeeds()
    {
        var inner = new ZLinkInMemoryLocationStore();
        var store = new FailOnceOwnerCleanupStore(inner);
        var options = new ZLinkLocationOptions
        {
            HeartbeatInterval = TimeSpan.FromMilliseconds(10),
            OwnerLeaseTtl = TimeSpan.FromSeconds(15)
        };
        var runtime = new ZLinkLocationRuntime(
            options,
            store,
            inner,
            inner,
            inner,
            inner,
            store);
        await runtime.StartAsync(RoutingId.From("node-1"));

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await runtime.CleanupOwnerForDrainAsync(CancellationToken.None));
        await store.HeartbeatAfterFailure.Task.WaitAsync(TimeSpan.FromSeconds(1));
        Assert.Single((await inner.ListOwnerLeasesAsync()).Leases);

        await runtime.CleanupOwnerForDrainAsync(CancellationToken.None);
        Assert.Empty((await inner.ListOwnerLeasesAsync()).Leases);
        await runtime.StopAsync();
    }

    [Fact]
    public async Task Startup_RequiresOwnerLease_AndCanRetryAfterTheStoreRecovers()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var flaky = new FlakyOwnerLeaseStore(store) { Fail = true };
        var runtime = NewRuntime(store, flaky, time);

        await Assert.ThrowsAsync<InvalidOperationException>(() =>
            runtime.StartAsync(RoutingId.From("node-1")).AsTask());
        Assert.Empty((await store.ListOwnerLeasesAsync()).Leases);

        flaky.Fail = false;
        await runtime.StartAsync(RoutingId.From("node-1"));
        Assert.Single((await store.ListOwnerLeasesAsync()).Leases);
        await runtime.StopAsync();
    }

    [Fact]
    public async Task Restart_UsesFreshOwnerGeneration_AndReportsStoppedLeaseUnhealthy()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtime = NewRuntime(store, store, time);

        await runtime.StartAsync(RoutingId.From("stable-node"));
        var firstOwner = runtime.OwnerId;
        Assert.True(runtime.GetHealthSnapshot().Healthy);

        await runtime.StopAsync();
        Assert.False(runtime.GetHealthSnapshot().Healthy);

        await runtime.StartAsync(RoutingId.From("stable-node"));
        Assert.NotEqual(firstOwner, runtime.OwnerId);
        Assert.True(runtime.GetHealthSnapshot().Healthy);
        await runtime.StopAsync();
    }

    [Fact]
    public void InMemory_Registration_Resolves_Resolvers_Query_And_Shared_Store()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options => options.UseInMemoryLocationStores());
        using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetRequiredService<IZLinkPeerLocationResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkSpotHandleResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkActorSpotHandleResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkLocationRuntimeQuery>());

        // The five store roles must share one physical store: in-memory
        // registration maps every interface onto one instance.
        var peerStore = provider.GetRequiredService<IZLinkPeerLocationStore>();
        var leaseStore = provider.GetRequiredService<IZLinkOwnerLeaseStore>();
        Assert.Same(peerStore, leaseStore);
    }

    [Fact]
    public async Task Registration_Path_Rejects_Values_Outside_The_Closed_Sets()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var runtime = NewRuntime(store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();

        // Readers ignore out-of-set rows; the registration path must never
        // produce one in the first place (draft 6.5 validation error).
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(async () =>
            await runtime.WritePeerAsync(
                InMemoryLocationStoreTests.Peer("ignored") with { Role = (ZLinkLocationRole)99 },
                ZLinkLocationWriteIntent.NewClaim));
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(async () =>
            await runtime.WritePeerAsync(
                InMemoryLocationStoreTests.Peer("ignored") with
                {
                    AutoConnectType = (ZLinkLocationAutoConnectType)77
                },
                ZLinkLocationWriteIntent.NewClaim));
    }

    private static ZLinkLocationRuntime NewRuntime(
        ZLinkInMemoryLocationStore store,
        IZLinkOwnerLeaseStore ownerLeaseStore,
        ManualTimeProvider time) =>
        new(new ZLinkLocationOptions(), store, store, store, store, store, ownerLeaseStore, time);

    private sealed class FlakyOwnerLeaseStore(IZLinkOwnerLeaseStore inner) : IZLinkOwnerLeaseStore
    {
        public bool Fail { get; set; }

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId,
            RoutingId nodeRid,
            TimeSpan leaseTtl,
            CancellationToken cancellationToken = default) =>
            Fail
                ? throw new InvalidOperationException("store unreachable")
                : inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }

    private sealed class FailOnceOwnerCleanupStore(IZLinkLocationStore inner) : IZLinkLocationStore
    {
        private int _fail = 1;
        private int _renewals;

        public TaskCompletionSource HeartbeatAfterFailure { get; } = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask<long> RemoveAllByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            Interlocked.Exchange(ref _fail, 0) != 0
                ? throw new InvalidOperationException("owner cleanup unavailable")
                : inner.RemoveAllByOwnerAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
            ZLinkPeerLocation peer, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdatePeerAsync(peer, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
            ZLinkPeerLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemovePeerAsync(key, owner, cancellationToken);

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter, CancellationToken cancellationToken = default) =>
            inner.ListPeersAsync(filter, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateSpotAsync(
            ZLinkSpotLocation spot, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateSpotAsync(spot, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
            ZLinkSpotLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveSpotAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
            ZLinkSpotLocationKey key, CancellationToken cancellationToken = default) =>
            inner.ResolveSpotAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
            ZLinkSpotLocationFilter filter, ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            inner.ListSpotsAsync(filter, page, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateActorAsync(actor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveActorAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key, CancellationToken cancellationToken = default) =>
            inner.ResolveActorAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter, ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            inner.ListActorsAsync(filter, page, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> UpdateRouteAsync(
            ZLinkRouteLocation route, ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateRouteAsync(route, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
            ZLinkRouteLocationKey key, ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveRouteAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
            ZLinkRouteLocationKey key, CancellationToken cancellationToken = default) =>
            inner.ResolveRouteAsync(key, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
            ZLinkRouteLocationFilter filter, ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            inner.ListRoutesAsync(filter, page, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseRenewal> RenewOwnerLeaseAsync(
            string ownerId, RoutingId nodeRid, TimeSpan leaseTtl,
            CancellationToken cancellationToken = default)
        {
            if (Interlocked.Increment(ref _renewals) >= 2)
                HeartbeatAfterFailure.TrySetResult();
            return inner.RenewOwnerLeaseAsync(ownerId, nodeRid, leaseTtl, cancellationToken);
        }

        public ValueTask<bool> RemoveOwnerLeaseAsync(
            string ownerId, CancellationToken cancellationToken = default) =>
            inner.RemoveOwnerLeaseAsync(ownerId, cancellationToken);

        public ValueTask<ZLinkOwnerLeaseSnapshot> ListOwnerLeasesAsync(
            CancellationToken cancellationToken = default) =>
            inner.ListOwnerLeasesAsync(cancellationToken);
    }
}
