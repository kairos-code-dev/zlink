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
    public async Task Readiness_Returns_False_When_Query_Fails()
    {
        var readiness = new ZLinkLocationReadiness(new FailingRuntimeQuery());

        var ready = await readiness.IsPeerReadyAsync(
            "play",
            ZLinkLocationRole.Router,
            RoutingId.From("node-1"));

        Assert.False(ready);
    }

    [Fact]
    public async Task Lists_Exclude_Rows_Of_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, LiveOwner, "1");
        await SeedRowsAsync(fixture.Store, DeadOwner, "2");

        // The dead owner stops heartbeating and its short lease runs out.
        fixture.Time.Advance(ShortLease + TimeSpan.FromSeconds(1));

        var peers = await fixture.Query.ListPeerLocationsAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        Assert.Equal(LiveOwner, Assert.Single(peers).OwnerId);

        var spots = await fixture.Query.ListSpotLocationsAsync(new ZLinkSpotLocationFilter(MeshName: "play"));
        Assert.Equal(LiveOwner, Assert.Single(spots.Items).OwnerId);

        var actors = await fixture.Query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(LiveOwner, Assert.Single(actors.Items).OwnerId);

        var routes = await fixture.Query.ListRouteLocationsAsync(new ZLinkRouteLocationFilter());
        Assert.Equal(LiveOwner, Assert.Single(routes.Items).OwnerId);
    }

    [Fact]
    public async Task Queries_Read_The_Store_Directly()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(LiveOwner, 0), ZLinkLocationWriteIntent.NewClaim);

        var key = new ZLinkActorLocationKey("actor-1");
        Assert.Equal(LiveOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(DeadOwner, 0) with { NodeRid = RoutingId.From("node-2") },
            ZLinkLocationWriteIntent.Takeover);

        // Without a resolver cache the takeover is visible immediately on
        // both surfaces; the runtime query reads the raw store rows.
        Assert.Equal(DeadOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);
        var queried = await fixture.Query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(DeadOwner, Assert.Single(queried.Items).OwnerId);
    }

    [Fact]
    public async Task Actor_List_Hides_Rows_Before_ActorRef_Is_Published()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(LiveOwner, 0) with { ActorRef = null },
            ZLinkLocationWriteIntent.NewClaim);

        var actors = await fixture.Query.ListActorLocationsAsync(new ZLinkActorLocationFilter());

        Assert.Empty(actors.Items);
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
            store, store, lagging, store, tracker, observed: observed);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, lagging, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, store, store, lagging, store, tracker, runtime, observed);

        var first = await query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Equal(2, Assert.Single(first.Items).Generation);

        // The lagging read must not roll the runtime's view backwards.
        var second = await query.ListActorLocationsAsync(new ZLinkActorLocationFilter());
        Assert.Empty(second.Items);
    }

    [Fact]
    public async Task Topology_And_Service_Summaries_Drop_Older_Peer_Generations()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var current = InMemoryLocationStoreTests.Peer(LiveOwner) with { Generation = 2 };
        var stale = current with { Generation = 1 };
        var peers = new ScriptedPeerListStore([current], [stale]);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, peers, store, store, store, tracker, runtime, observed);

        var topology = await query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(Kind: ZLinkLocationKind.Peer));
        var summaries = await query.ListServiceSummariesAsync(
            new ZLinkLocationServiceSummaryFilter());

        Assert.Single(topology.Items);
        Assert.Empty(summaries);
    }

    [Fact]
    public async Task Topology_Applies_State_And_Actor_Mesh_Filters()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(LiveOwner, 0) with { SpotMeshName = "play" },
            ZLinkLocationWriteIntent.NewClaim);

        var ready = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(
                Kind: ZLinkLocationKind.Actor,
                MeshName: "play",
                State: ZLinkLocationTopologyState.Ready));
        var lost = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(
                Kind: ZLinkLocationKind.Actor,
                MeshName: "play",
                State: ZLinkLocationTopologyState.Lost));
        var otherMesh = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(
                Kind: ZLinkLocationKind.Actor,
                MeshName: "other"));

        Assert.Single(ready.Items);
        Assert.Empty(lost.Items);
        Assert.Empty(otherMesh.Items);
    }

    [Theory]
    [InlineData(ZLinkLocationKind.Spot)]
    [InlineData(ZLinkLocationKind.Actor)]
    [InlineData(ZLinkLocationKind.Route)]
    public async Task Topology_Projects_Lost_State_For_Expired_Owners(
        ZLinkLocationKind kind)
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, DeadOwner, "lost");
        fixture.Time.Advance(ShortLease + TimeSpan.FromSeconds(1));

        var topology = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(Kind: kind, State: ZLinkLocationTopologyState.Lost));

        Assert.Single(topology.Items);
        Assert.Equal(ZLinkLocationTopologyState.Lost, topology.Items[0].State);
    }

    [Fact]
    public async Task Status_Reports_Watch_Capability_And_Shared_Read_Failures()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time);
        var health = new ZLinkLocationStoreHealth();
        var query = new ZLinkLocationRuntimeQueryService(
            options,
            store,
            store,
            store,
            store,
            tracker,
            runtime,
            new ZLinkObservedLocationGenerations(),
            watchEnabled: true,
            storeHealth: health);
        health.ReportFailure("peer-query-read", new InvalidOperationException("read unavailable"));

        var status = await query.GetStatusAsync();

        Assert.True(status.WatchEnabled);
        Assert.False(status.StoreHealthy);
        Assert.Contains("read unavailable", status.LastError, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Store_Health_Distinguishes_Caller_Cancellation_From_Internal_Cancellation()
    {
        var callerHealth = new ZLinkLocationStoreHealth();
        using var canceled = new CancellationTokenSource();
        canceled.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await ZLinkLocationStoreRead.ExecuteAsync<int>(
                callerHealth,
                "caller",
                canceled.Token,
                () => ValueTask.FromException<int>(new OperationCanceledException())));

        var internalHealth = new ZLinkLocationStoreHealth();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await ZLinkLocationStoreRead.ExecuteAsync<int>(
                internalHealth,
                "internal",
                CancellationToken.None,
                () => ValueTask.FromException<int>(new OperationCanceledException("store timeout"))));

        Assert.True(callerHealth.GetSnapshot().Healthy);
        Assert.False(internalHealth.GetSnapshot().Healthy);
    }

    private sealed class ScriptedPeerListStore(params ZLinkPeerLocation[][] pages) : IZLinkPeerLocationStore
    {
        private readonly Queue<ZLinkPeerLocation[]> _pages = new(pages);

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IReadOnlyList<ZLinkPeerLocation>>(
                _pages.Count > 0 ? _pages.Dequeue() : []);

        public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
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

    }

    private sealed class FailingRuntimeQuery : IZLinkLocationRuntimeQuery
    {
        public ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeerLocationsAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotLocationsAsync(
            ZLinkSpotLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorLocationsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRouteLocationsAsync(
            ZLinkRouteLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
            ZLinkLocationTopologyFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");

        public ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
            ZLinkLocationServiceSummaryFilter filter,
            CancellationToken cancellationToken = default) =>
            throw new InvalidOperationException("store unavailable");
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
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, store, tracker, observed);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, store, store, store, store, tracker, runtime, observed);
        return new QueryFixture(store, resolvers, query, time);
    }

    private sealed record QueryFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ZLinkLocationRuntimeQueryService Query,
        ManualTimeProvider Time);
}
