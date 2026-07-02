using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class LocationResolverTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);
    private static readonly ZLinkActorLocationKey ActorKey = new("player", "actor-1");

    [Fact]
    public async Task Normal_Uses_Cache_And_Refresh_Reads_The_Store()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        var first = await fixture.Resolvers.ResolveActorAsync(ActorKey);
        Assert.NotNull(first);
        Assert.Equal(OwnerA, first.OwnerId);

        // The row moves to owner B behind the cache's back.
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);

        // Normal within the TTL still serves the cached row; Refresh must
        // bypass the cache and see the takeover, then repair the cache.
        var cachedHit = await fixture.Resolvers.ResolveActorAsync(ActorKey);
        Assert.Equal(OwnerA, cachedHit!.OwnerId);

        var refreshed = await fixture.Resolvers.ResolveActorAsync(
            ActorKey, ZLinkResolveFreshness.Refresh);
        Assert.Equal(OwnerB, refreshed!.OwnerId);

        var repaired = await fixture.Resolvers.ResolveActorAsync(ActorKey);
        Assert.Equal(OwnerB, repaired!.OwnerId);
    }

    [Fact]
    public async Task NotFound_Is_Never_Cached()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorAsync(ActorKey));

        // An actor created right after a miss must be visible to the very
        // next Normal resolve — a cached not-found would break the
        // create-if-absent race.
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        Assert.NotNull(await fixture.Resolvers.ResolveActorAsync(ActorKey));
    }

    [Fact]
    public async Task Rows_Of_Expired_Owner_Are_Not_Returned_Even_On_Cache_Hits()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.NotNull(await fixture.Resolvers.ResolveActorAsync(ActorKey));

        // Owner A crashes: no more heartbeats, the lease runs out. The row
        // itself is never written again, yet resolve must stop returning it.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveActorAsync(ActorKey));
    }

    [Fact]
    public async Task Peer_List_Excludes_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));
        await fixture.Store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerB, "tcp://127.0.0.1:5002", "node-2"),
            ZLinkLocationWriteIntent.NewClaim);

        var filter = new ZLinkPeerLocationFilter(MeshName: "play");
        var both = await fixture.Resolvers.ListPeersAsync(filter);
        Assert.Equal(2, both.Count);

        // Owner A's lease expires; the peer drops out of the desired set
        // within one polling interval without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var survivors = await fixture.Resolvers.ListPeersAsync(filter, ZLinkResolveFreshness.Refresh);
        Assert.Single(survivors);
        Assert.Equal(OwnerB, survivors[0].OwnerId);
    }

    [Fact]
    public async Task Disabled_Cache_Reads_The_Store_With_Identical_Results()
    {
        var fixture = await FixtureAsync(options => options.ActorCacheEnabled = false);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        Assert.NotNull(await fixture.Resolvers.ResolveActorAsync(ActorKey));

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);

        // With the cache off, Normal sees the takeover immediately: the
        // result meaning is identical, only the read path changes.
        var current = await fixture.Resolvers.ResolveActorAsync(ActorKey);
        Assert.Equal(OwnerB, current!.OwnerId);
        Assert.Equal(0, fixture.Resolvers.ActorCacheEntryCount);
    }

    [Fact]
    public async Task Actor_Type_Null_And_Empty_Are_The_Same_Key()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0) with { ActorType = "" },
            ZLinkLocationWriteIntent.NewClaim);

        var byEmpty = await fixture.Resolvers.ResolveActorAsync(new ZLinkActorLocationKey("", "actor-1"));
        var byNull = await fixture.Resolvers.ResolveActorAsync(new ZLinkActorLocationKey(null!, "actor-1"));

        Assert.NotNull(byEmpty);
        Assert.NotNull(byNull);
        Assert.Equal(byEmpty.ActorId, byNull.ActorId);
    }

    [Fact]
    public async Task Older_Generation_From_A_Lagging_Replica_Is_Never_A_Success()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A replica store that first serves generation 2, then lags back to
        // generation 1, then catches up again.
        var replica = new ScriptedActorStore(
            InMemoryLocationStoreTests.Actor(OwnerA, 2),
            InMemoryLocationStoreTests.Actor(OwnerA, 1),
            InMemoryLocationStoreTests.Actor(OwnerA, 2));
        var resolvers = new ZLinkStoreLocationResolvers(
            options, store, store, replica, store, tracker, time);

        var first = await resolvers.ResolveActorAsync(ActorKey, ZLinkResolveFreshness.Refresh);
        Assert.Equal(2, first!.Generation);

        // The lagging read must not roll the observed view backwards.
        Assert.Null(await resolvers.ResolveActorAsync(ActorKey, ZLinkResolveFreshness.Refresh));

        // An equal generation is accepted again.
        var third = await resolvers.ResolveActorAsync(ActorKey, ZLinkResolveFreshness.Refresh);
        Assert.Equal(2, third!.Generation);
    }

    private sealed class ScriptedActorStore(params ZLinkActorLocation[] rows) : IZLinkActorLocationStore
    {
        private readonly Queue<ZLinkActorLocation> _rows = new(rows);

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkActorLocation?>(_rows.Count > 0 ? _rows.Dequeue() : null);

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<long> RemoveByOwnerAsync(
            string ownerId,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private static async Task<ResolverFixture> FixtureAsync(
        Action<ZLinkLocationOptions>? configure = null)
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));

        var options = new ZLinkLocationOptions
        {
            // Keep the lease snapshot maximally fresh in unit tests so lease
            // expiry is observed on the next read.
            PollingInterval = TimeSpan.Zero
        };
        configure?.Invoke(options);

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(
            options, store, store, store, store, tracker, time);
        return new ResolverFixture(store, resolvers, time);
    }

    private sealed record ResolverFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ManualTimeProvider Time);
}
