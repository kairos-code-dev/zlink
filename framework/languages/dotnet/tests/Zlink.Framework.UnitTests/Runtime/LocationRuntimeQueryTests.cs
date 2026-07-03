using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

/// <summary>
/// Draft 8.2: the runtime query surface reads the stores directly — no
/// cache, no freshness — and excludes rows of expired owners from every
/// success result.
/// </summary>
public sealed class LocationRuntimeQueryTests
{
    private const string LiveOwner = "live-owner";
    private const string DeadOwner = "dead-owner";
    private static readonly TimeSpan ShortLease = TimeSpan.FromSeconds(15);

    [Fact]
    public async Task Lists_Exclude_Rows_Of_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, LiveOwner, "1");
        await SeedRowsAsync(fixture.Store, DeadOwner, "2");

        // The dead owner stops heartbeating and its short lease runs out.
        fixture.Time.Advance(ShortLease + TimeSpan.FromSeconds(1));

        var peers = await fixture.Query.ListPeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Equal(LiveOwner, Assert.Single(peers).OwnerId);

        var spots = await fixture.Query.ListSpotsAsync(new ZLinkSpotLocationFilter(MeshName: "play"));
        Assert.Equal(LiveOwner, Assert.Single(spots.Items).OwnerId);

        var actors = await fixture.Query.ListActorsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(LiveOwner, Assert.Single(actors.Items).OwnerId);

        var routes = await fixture.Query.ListRoutesAsync(new ZLinkRouteLocationFilter());
        Assert.Equal(LiveOwner, Assert.Single(routes.Items).OwnerId);
    }

    [Fact]
    public async Task Queries_Read_The_Store_Directly()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(LiveOwner, 0), ZLinkLocationWriteIntent.NewClaim);

        var key = new ZLinkActorLocationKey("player", "actor-1");
        Assert.Equal(LiveOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(DeadOwner, 0) with { NodeRid = RoutingId.From("node-2") },
            ZLinkLocationWriteIntent.Takeover);

        // Without a resolver cache the takeover is visible immediately on
        // both surfaces; the runtime query reads the raw store rows.
        Assert.Equal(DeadOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);
        var queried = await fixture.Query.ListActorsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(DeadOwner, Assert.Single(queried.Items).OwnerId);
    }

    [Fact]
    public async Task Lists_Drop_Rows_Older_Than_An_Observed_Generation()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A replica that first serves generation 2 and then lags back to 1.
        var lagging = new ScriptedActorListStore(
            [InMemoryLocationStoreTests.Actor(LiveOwner, 2)],
            [InMemoryLocationStoreTests.Actor(LiveOwner, 1)]);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            options, store, store, lagging, store, tracker, time, observed: observed);
        var runtime = new ZLinkLocationRuntime(options, store, store, lagging, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, store, store, lagging, store, tracker, runtime, resolvers, observed);

        var first = await query.ListActorsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(2, Assert.Single(first.Items).Generation);

        // The lagging read must not roll the runtime's view backwards.
        var second = await query.ListActorsAsync(new ZLinkActorLocationFilter());
        Assert.Empty(second.Items);
    }

    private sealed class ScriptedActorListStore(params ZLinkActorLocation[][] pages) : IZLinkActorLocationStore
    {
        private readonly Queue<ZLinkActorLocation[]> _pages = new(pages);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult(new ZLinkLocationPage<ZLinkActorLocation>(
                _pages.Count > 0 ? _pages.Dequeue() : [], null));

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

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
    }

    private static async Task SeedRowsAsync(ZLinkInMemoryLocationStore store, string owner, string suffix)
    {
        await store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(owner, $"tcp://127.0.0.1:500{suffix}", $"node-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(owner, $"spot-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(owner, 0, $"actor-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateRouteAsync(
            InMemoryLocationStoreTests.Route(owner, $"route-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
    }

    private static async Task<QueryFixture> FixtureAsync()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        await store.RenewOwnerLeaseAsync(DeadOwner, RoutingId.From("node-2"), ShortLease);

        var options = new ZLinkLocationOptions
        {
            PollingInterval = TimeSpan.Zero,
            // Keep resolver cache entries alive across the test so the
            // bypass assertion contrasts cache hits with direct reads.
        };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(
            options, store, store, store, store, tracker, time);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, store, store, store, store, tracker, runtime, resolvers);
        return new QueryFixture(store, resolvers, query, time);
    }

    private sealed record QueryFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        IZLinkLocationRuntimeQuery Query,
        ManualTimeProvider Time);
}
