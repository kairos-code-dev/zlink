using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class AutoConnectReconcilerTests
{
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

    [Theory]
    [InlineData(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router, true)]
    [InlineData(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Dealer, false)]
    [InlineData(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, true)]
    [InlineData(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Dealer, true)]
    [InlineData(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Pub, false)]
    [InlineData(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, true)]
    [InlineData(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Router, false)]
    [InlineData(ZLinkLocationAutoConnectType.Fanout, ZLinkLocationRole.Pub, true)]
    [InlineData(ZLinkLocationAutoConnectType.Fanout, ZLinkLocationRole.Sub, true)]
    [InlineData(ZLinkLocationAutoConnectType.Fanout, ZLinkLocationRole.Spot, false)]
    [InlineData(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot, true)]
    [InlineData(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Router, true)]
    [InlineData(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Sub, false)]
    public void Role_Allow_Table_Matches_The_Contract(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role,
        bool allowed)
    {
        Assert.Equal(allowed, ZLinkAutoConnectPlanner.IsRoleAllowed(type, role));
    }

    [Fact]
    public void Planner_Excludes_Self_And_Direction_Violations()
    {
        var local = Local(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Dealer, "local", "tcp://l:1");
        var peers = new[]
        {
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, "r1", "tcp://r:1"),
            // Same rid as local: excluded as self.
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, "local", "tcp://r:2"),
            // A dealer never dials another dealer in client/server.
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Dealer, "d1", "tcp://d:1"),
            // Different mesh: ignored.
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, "r2", "tcp://r:3", mesh: "other")
        };

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(local, peers);

        var target = Assert.Single(desired.Values);
        Assert.Equal("tcp://r:1", target.Endpoint);
    }

    [Fact]
    public void Dealer_Mesh_Pairwise_Initiator_Connects_From_The_Smaller_Side_Only()
    {
        var smaller = Local(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, "aa", "tcp://a:1");
        var bigger = Local(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, "bb", "tcp://b:1");
        var rowSmaller = Peer(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, "aa", "tcp://a:1");
        var rowBigger = Peer(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, "bb", "tcp://b:1");

        var fromSmaller = ZLinkAutoConnectPlanner.ComputeDesired(smaller, [rowBigger]);
        var fromBigger = ZLinkAutoConnectPlanner.ComputeDesired(bigger, [rowSmaller]);

        // Only the byte-order smaller routing id dials, so the pair never
        // double-connects.
        Assert.Single(fromSmaller);
        Assert.Empty(fromBigger);
    }

    [Fact]
    public async Task Reconcile_Connects_New_Targets_And_Disconnects_Vanished_Ones()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.PublishPeerAsync("r2", "tcp://r:2");

        await fixture.Reconciler.TickAsync();
        Assert.Equal(2, fixture.Executor.Connected.Count);

        await fixture.RemovePeerAsync("r2", "tcp://r:2");
        await fixture.Reconciler.TickAsync();

        var disconnected = Assert.Single(fixture.Executor.Disconnected);
        Assert.Equal("tcp://r:2", disconnected.Endpoint);
        Assert.Single(fixture.Reconciler.ActiveTargets);
    }

    [Fact]
    public async Task Endpoint_Change_For_The_Same_Peer_Key_Is_A_Handover()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        await fixture.PublishPeerAsync("r1", "tcp://r:9", takeover: true);
        await fixture.Reconciler.TickAsync();

        Assert.Equal(["tcp://r:1", "tcp://r:9"], fixture.Executor.Connected.Select(t => t.Endpoint));
        var dropped = Assert.Single(fixture.Executor.Disconnected);
        Assert.Equal("tcp://r:1", dropped.Endpoint);
    }

    [Fact]
    public async Task Store_Outage_Is_Fail_Static_And_Recovery_Defers_Disconnects()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Reconciler.ActiveTargets);

        // Outage: the tick keeps the last desired set and cuts nothing.
        fixture.PeerResolver.Fail = true;
        await fixture.Reconciler.TickAsync();
        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Single(fixture.Reconciler.ActiveTargets);

        // Recovery with an empty store: r1 has not re-registered yet. The
        // first tick must not cut it — disconnect diffs wait one heartbeat
        // interval so the mesh does not sweep live peers.
        await fixture.RemovePeerAsync("r1", "tcp://r:1");
        fixture.PeerResolver.Fail = false;
        await fixture.Reconciler.TickAsync();
        Assert.Empty(fixture.Executor.Disconnected);

        // After the grace the fresh list wins and the vanished peer drops.
        fixture.Time.Advance(TimeSpan.FromSeconds(6));
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Executor.Disconnected);
    }

    [Fact]
    public async Task Recovery_Republishes_The_Local_Row_Before_Reading_The_List()
    {
        var fixture = await FixtureAsync();
        await fixture.Reconciler.TickAsync();
        var published = await fixture.Store.ListPeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play", Role: ZLinkLocationRole.Dealer));
        Assert.Single(published);

        // Outage long enough for the local lease to expire and the row to
        // be claimable again.
        fixture.PeerResolver.Fail = true;
        await fixture.Reconciler.TickAsync();
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));
        await fixture.Runtime.RenewOwnerLeaseOnceAsync();

        fixture.PeerResolver.Fail = false;
        await fixture.Reconciler.TickAsync();

        published = await fixture.Store.ListPeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play", Role: ZLinkLocationRole.Dealer));
        Assert.Single(published);
        Assert.Equal(fixture.Runtime.OwnerId, published[0].OwnerId);
    }

    private static ZLinkAutoConnectLocal Local(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role,
        string rid,
        string endpoint) =>
        new(type, "play", role, RoutingId.From(rid), endpoint);

    private static ZLinkPeerLocation Peer(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role,
        string rid,
        string endpoint,
        string mesh = "play") =>
        new(type, mesh, RoutingId.From(rid), role, endpoint, 100, 0, null, null, "peer-owner", 1, default);

    private static async Task<ReconcilerFixture> FixtureAsync()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("peer-node"), TimeSpan.FromMinutes(10));

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(options, store, store, store, store, tracker, time);
        var failable = new FailablePeerResolver(resolvers);
        var executor = new RecordingExecutor();
        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            RoutingId.From("local"), "tcp://l:1");
        var localRow = new ZLinkPeerLocation(
            local.AutoConnectType, local.MeshName, local.NodeRid, local.Role, local.Endpoint,
            100, 0, null, null, "ignored", 0, default);
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow, runtime, failable, executor, options, time);
        return new ReconcilerFixture(store, runtime, failable, executor, reconciler, time);
    }

    private sealed record ReconcilerFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkLocationRuntime Runtime,
        FailablePeerResolver PeerResolver,
        RecordingExecutor Executor,
        ZLinkAutoConnectReconciler Reconciler,
        ManualTimeProvider Time)
    {
        public async Task PublishPeerAsync(string rid, string endpoint, bool takeover = false)
        {
            var row = Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, rid, endpoint);
            await Store.UpdatePeerAsync(
                row,
                takeover ? ZLinkLocationWriteIntent.Takeover : ZLinkLocationWriteIntent.NewClaim);
        }

        public async Task RemovePeerAsync(string rid, string endpoint)
        {
            var current = await Store.ListPeersAsync(new ZLinkPeerLocationFilter(Endpoint: endpoint));
            foreach (var row in current)
            {
                await Store.RemovePeerAsync(
                    new ZLinkPeerLocationKey(row.AutoConnectType, row.MeshName, row.Role, row.NodeRid, row.Endpoint),
                    new ZLinkLocationOwnerToken(row.OwnerId, row.Generation));
            }
        }
    }

    private sealed class FailablePeerResolver(IZLinkPeerLocationResolver inner) : IZLinkPeerLocationResolver
    {
        public bool Fail { get; set; }

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
            CancellationToken cancellationToken = default) =>
            Fail
                ? throw new InvalidOperationException("store unreachable")
                : inner.ListPeersAsync(filter, freshness, cancellationToken);
    }

    private sealed class RecordingExecutor : IZLinkAutoConnectExecutor
    {
        public List<ZLinkAutoConnectTarget> Connected { get; } = [];

        public List<ZLinkAutoConnectTarget> Disconnected { get; } = [];

        public void Connect(ZLinkAutoConnectTarget target) => Connected.Add(target);

        public void Disconnect(ZLinkAutoConnectTarget target) => Disconnected.Add(target);
    }
}
