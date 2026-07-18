using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

/// <summary>
/// The runtime query surface reads the registered store directly — no
/// cache, no freshness — and excludes rows of expired owners from every
/// success result. Spot and Actor rows are resolve-only store records, so
/// topology and summaries project MeshNode descriptors only.
/// </summary>
public sealed class LocationRuntimeQueryTests
{
    private const string LiveOwner = "live-owner";
    private const string DeadOwner = "dead-owner";
    private static readonly TimeSpan ShortLease = TimeSpan.FromSeconds(15);
    private static readonly string[] RegisteredMeshes = ["play"];

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
    public async Task Descriptor_List_Excludes_Rows_Of_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, LiveOwner, "1");
        await SeedRowsAsync(fixture.Store, DeadOwner, "2");

        // The dead owner stops heartbeating and its short lease runs out.
        fixture.Time.Advance(ShortLease + TimeSpan.FromSeconds(1));

        var descriptors = await fixture.Query.ListMeshNodeDescriptorsAsync("play");

        Assert.Equal(LiveOwner, Assert.Single(descriptors).OwnerId);
    }

    [Fact]
    public async Task Queries_Read_The_Store_Directly()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(LiveOwner), ZLinkLocationWriteIntent.NewClaim);

        var key = new ZLinkActorLocationKey("play", "actor-1");
        Assert.Equal(LiveOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(DeadOwner) with
            {
                OwnerNodeRid = RoutingId.From("node-2")
            },
            ZLinkLocationWriteIntent.Takeover);

        // Without a resolver cache the takeover is visible immediately; the
        // resolve surface reads the raw store rows.
        Assert.Equal(DeadOwner, (await fixture.Resolvers.ResolveActorRowAsync(key))!.OwnerId);
    }

    [Fact]
    public async Task Actor_Resolve_Drops_Views_Older_Than_An_Observed_Membership_Epoch()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A replica that first serves membership epoch 2 and then lags back
        // to epoch 1.
        var lagging = new ScriptedActorResolveStore(
            InMemoryLocationStoreTests.Actor(LiveOwner) with { MembershipEpoch = 2 },
            InMemoryLocationStoreTests.Actor(LiveOwner) with { MembershipEpoch = 1 });
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, lagging, tracker, observed);

        var key = new ZLinkActorLocationKey("play", "actor-1");
        var first = await resolvers.ResolveActorRowAsync(key);
        Assert.Equal(2UL, first!.MembershipEpoch);

        // The lagging read must not roll the runtime's view backwards.
        var second = await resolvers.ResolveActorRowAsync(key);
        Assert.Null(second);
    }

    [Fact]
    public async Task Topology_And_Service_Summaries_Drop_Older_Descriptor_Revisions()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var current = InMemoryLocationStoreTests.MeshNode(LiveOwner) with { DescriptorRevision = 2 };
        var stale = current with { DescriptorRevision = 1 };
        var peers = new ScriptedMeshNodeListStore([current], [stale]);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, peers, RegisteredMeshes, tracker, runtime, observed);

        var topology = await query.ListTopologyAsync(new ZLinkLocationTopologyFilter());
        var summaries = await query.ListServiceSummariesAsync(
            new ZLinkLocationServiceSummaryFilter());

        Assert.Single(topology.Items);
        Assert.Empty(summaries);
    }

    [Fact]
    public async Task Topology_Applies_Mesh_And_State_Filters()
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, LiveOwner, "1");

        var ready = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(
                MeshName: "play",
                State: ZLinkLocationTopologyState.Ready));
        var lost = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(
                MeshName: "play",
                State: ZLinkLocationTopologyState.Lost));
        var otherMesh = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(MeshName: "other"));

        Assert.Single(ready.Items);
        Assert.Empty(lost.Items);
        Assert.Empty(otherMesh.Items);
    }

    [Fact]
    public async Task Topology_Projects_Lost_State_For_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await SeedRowsAsync(fixture.Store, DeadOwner, "9");
        fixture.Time.Advance(ShortLease + TimeSpan.FromSeconds(1));

        var topology = await fixture.Query.ListTopologyAsync(
            new ZLinkLocationTopologyFilter(State: ZLinkLocationTopologyState.Lost));

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
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        var health = new ZLinkLocationStoreHealth();
        var query = new ZLinkLocationRuntimeQueryService(
            options,
            store,
            RegisteredMeshes,
            tracker,
            runtime,
            new ZLinkObservedLocationGenerations(),
            watchEnabled: true,
            storeHealth: health);
        health.ReportFailure("mesh-node-query-read", new InvalidOperationException("read unavailable"));

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
                _ => ValueTask.FromException<int>(new OperationCanceledException())));

        var internalHealth = new ZLinkLocationStoreHealth();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await ZLinkLocationStoreRead.ExecuteAsync<int>(
                internalHealth,
                "internal",
                CancellationToken.None,
                _ => ValueTask.FromException<int>(new OperationCanceledException("store timeout"))));

        Assert.True(callerHealth.GetSnapshot().Healthy);
        Assert.False(internalHealth.GetSnapshot().Healthy);
    }

    private sealed class ScriptedMeshNodeListStore(params ZLinkMeshNodeDescriptor[][] pages)
        : IZLinkMeshNodeLocationStore
    {
        private readonly Queue<ZLinkMeshNodeDescriptor[]> _pages = new(pages);

        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodesAsync(
            string meshName,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<IReadOnlyList<ZLinkMeshNodeDescriptor>>(
                _pages.Count > 0 ? _pages.Dequeue() : []);

        public ValueTask<ZLinkLocationWriteResult> UpdateMeshNodeAsync(
            ZLinkMeshNodeDescriptor descriptor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationWriteStatus> RemoveMeshNodeAsync(
            ZLinkMeshNodeDescriptorKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private sealed class ScriptedActorResolveStore(params ZLinkActorLocation[] rows)
        : IZLinkActorLocationStore
    {
        private readonly Queue<ZLinkActorLocation> _rows = new(rows);

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default) =>
            ValueTask.FromResult<ZLinkActorLocation?>(
                _rows.Count > 0 ? _rows.Dequeue() : null);

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
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

        public ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListMeshNodeDescriptorsAsync(
            string meshName,
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
        await store.UpdateMeshNodeAsync(
            InMemoryLocationStoreTests.MeshNode(owner, $"tcp://127.0.0.1:500{suffix}", $"node-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(owner, $"spot-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(owner, $"actor-{suffix}"),
            ZLinkLocationWriteIntent.NewClaim);
    }

    private static async Task<QueryFixture> FixtureAsync()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(LiveOwner, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        await store.RenewOwnerLeaseAsync(DeadOwner, RoutingId.From("node-2"), ShortLease);

        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, tracker, observed);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        var query = new ZLinkLocationRuntimeQueryService(
            options, store, RegisteredMeshes, tracker, runtime, observed);
        return new QueryFixture(store, resolvers, query, time);
    }

    private sealed record QueryFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ZLinkLocationRuntimeQueryService Query,
        ManualTimeProvider Time);
}
