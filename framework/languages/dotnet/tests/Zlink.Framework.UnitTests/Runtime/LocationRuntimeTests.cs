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
        Assert.True(runtime.OwnerLeaseHealthy);
        Assert.Null(runtime.LastError);

        // Fail-static: a store outage is recorded, never thrown, and the
        // next successful renew clears the error.
        flaky.Fail = true;
        Assert.False(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.False(runtime.OwnerLeaseHealthy);
        Assert.NotNull(runtime.LastError);

        flaky.Fail = false;
        Assert.True(await runtime.RenewOwnerLeaseOnceAsync());
        Assert.True(runtime.OwnerLeaseHealthy);
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
        Assert.True(runtime.OwnerLeaseHealthy);

        await runtime.StopAsync();
        Assert.False(runtime.OwnerLeaseHealthy);

        await runtime.StartAsync(RoutingId.From("stable-node"));
        Assert.NotEqual(firstOwner, runtime.OwnerId);
        Assert.True(runtime.OwnerLeaseHealthy);
        await runtime.StopAsync();
    }

    [Fact]
    public void InMemory_Registration_Resolves_Resolvers_Query_And_Shared_Store()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options => options.UseInMemoryLocationStores());
        using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetRequiredService<IZLinkPeerLocationResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkSpotRefResolver>());
        Assert.NotNull(provider.GetRequiredService<IZLinkActorAddressResolver>());
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
}
