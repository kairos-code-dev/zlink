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

    [Theory]
    [InlineData(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer)]
    [InlineData(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router)]
    [InlineData(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot)]
    public void Symmetric_Mesh_Pairwise_Initiator_Connects_From_The_Smaller_Side_Only(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role)
    {
        var smaller = Local(type, role, "aa", "tcp://a:1");
        var bigger = Local(type, role, "bb", "tcp://b:1");
        var rowSmaller = Peer(type, role, "aa", "tcp://a:1");
        var rowBigger = Peer(type, role, "bb", "tcp://b:1");

        var fromSmaller = ZLinkAutoConnectPlanner.ComputeDesired(smaller, [rowBigger]);
        var fromBigger = ZLinkAutoConnectPlanner.ComputeDesired(bigger, [rowSmaller]);

        // Only the byte-order smaller routing id dials, so route, spot,
        // and dealer meshes share one physical link per peer pair.
        Assert.Single(fromSmaller);
        Assert.Empty(fromBigger);
    }

    [Fact]
    public void Endpoint_Less_Mesh_Member_Always_Dials_Dialable_Peers()
    {
        // "zz" sorts after "aa", so the plain initiator rule would tell the
        // endpoint-less member to wait — but nobody can dial it, so it must
        // initiate regardless of the id order.
        var dialOnly = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.RouteMesh, "play", ZLinkLocationRole.Router,
            RoutingId.From("zz"), string.Empty);
        var server = Peer(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router, "aa", "tcp://a:1");

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(dialOnly, [server]);

        Assert.Single(desired);
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
    public async Task Membership_Snapshot_Classifies_Known_And_Unknown_Peers()
    {
        var fixture = await FixtureAsync();

        // No judgment before the first successful reconcile.
        Assert.Null(fixture.Reconciler.KnowsPeer(RoutingId.From("r1")));

        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        Assert.True(fixture.Reconciler.KnowsPeer(RoutingId.From("r1")));
        Assert.False(fixture.Reconciler.KnowsPeer(RoutingId.From("ghost")));

        // Fail-static: a store outage keeps the last snapshot.
        fixture.PeerResolver.Fail = true;
        await fixture.Reconciler.TickAsync();
        Assert.True(fixture.Reconciler.KnowsPeer(RoutingId.From("r1")));
    }

    [Fact]
    public void Membership_Includes_Peers_The_Initiator_Rule_Excludes_From_Dialing()
    {
        // "aa" dials "zz" never (zz initiates), yet zz is a reachable
        // rid-addressed target once it dials us: membership, not the
        // desired dial set, is the fail-fast knowledge source.
        var local = Local(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router, "zz", "tcp://z:1");
        var peer = Peer(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router, "aa", "tcp://a:1");

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(local, [peer]);

        Assert.Empty(desired);
    }

    [Fact]
    public async Task Owner_Change_For_The_Same_Peer_Key_Is_A_Handover()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        // The peer restarts: same rid and endpoint, re-claimed by a new
        // owner. The old dial points at a dead process, so the reconciler
        // must replace the connection even though nothing else changed.
        await fixture.Store.RenewOwnerLeaseAsync(
            "peer-owner-2", RoutingId.From("peer-node-2"), TimeSpan.FromMinutes(10));
        var restarted = Peer(
            ZLinkLocationAutoConnectType.ClientServer,
            ZLinkLocationRole.Router,
            "r1",
            "tcp://r:1") with { OwnerId = "peer-owner-2" };
        await fixture.Store.UpdatePeerAsync(restarted, ZLinkLocationWriteIntent.Takeover);
        await fixture.Reconciler.TickAsync();

        Assert.Equal(["tcp://r:1", "tcp://r:1"], fixture.Executor.Connected.Select(t => t.Endpoint));
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
    public async Task StoreFailureGrace_Keeps_Ready_Connections_And_Blocks_New_Outbound_During_Outage()
    {
        var fixture = await FixtureAsync(options => options.StoreFailureGrace = TimeSpan.FromSeconds(3));
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        Assert.Equal("tcp://r:1", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);

        fixture.PeerResolver.Fail = true;
        await fixture.RemovePeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Equal("tcp://r:1", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);

        // Even after the grace boundary, the old ready connection stays up.
        // A new peer that appears in the store during the outage is not dialed
        // because fail-static ticks do not compute an expanded desired set.
        await fixture.PublishPeerAsync("r2", "tcp://r:2");
        fixture.Time.Advance(TimeSpan.FromSeconds(4));
        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Equal(["tcp://r:1"], fixture.Executor.Connected.Select(target => target.Endpoint));
        Assert.Equal("tcp://r:1", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);

        // Recovery re-publishes the local row before reading the list, then
        // connects newly visible peers immediately but still defers disconnect
        // diffs for one heartbeat interval.
        fixture.PeerResolver.Fail = false;
        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Equal(["tcp://r:1", "tcp://r:2"], fixture.Executor.Connected.Select(target => target.Endpoint));
        Assert.Equal(
            ["tcp://r:1", "tcp://r:2"],
            fixture.Reconciler.ActiveTargets.Select(target => target.Endpoint).Order());

        fixture.Time.Advance(TimeSpan.FromSeconds(6));
        await fixture.Reconciler.TickAsync();

        Assert.Equal("tcp://r:1", Assert.Single(fixture.Executor.Disconnected).Endpoint);
        Assert.Equal("tcp://r:2", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);
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

    [Fact]
    public async Task Dial_Only_Capability_Without_Identity_Still_Connects_And_Never_Advertises()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("peer-node"), TimeSpan.FromMinutes(10));
        await store.UpdatePeerAsync(
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Router, "r1", "tcp://r:1"),
            ZLinkLocationWriteIntent.NewClaim);

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(options, store, store, store, store, tracker, time);
        var executor = new RecordingExecutor();

        // An EnableClient() dealer has neither a routing id nor an endpoint:
        // it cannot be keyed, so it publishes no row — but it must dial.
        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            NodeRid: null, Endpoint: string.Empty);
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow: null, runtime, resolvers, executor, options, time);

        await reconciler.TickAsync();

        Assert.Equal("tcp://r:1", Assert.Single(executor.Connected).Endpoint);
        var dealers = await store.ListPeersAsync(
            new ZLinkPeerLocationFilter(MeshName: "play", Role: ZLinkLocationRole.Dealer));
        Assert.Empty(dealers);

        // Shutdown has no row to remove and only tears down connections.
        await reconciler.ShutdownAsync();
        Assert.Single(executor.Disconnected);
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

    private static async Task<ReconcilerFixture> FixtureAsync(Action<ZLinkLocationOptions>? configure = null)
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        configure?.Invoke(options);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, store, time);
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

        public ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default) =>
            Fail
                ? throw new InvalidOperationException("store unreachable")
                : inner.ListLivePeersAsync(filter, cancellationToken);
    }

    private sealed class RecordingExecutor : IZLinkAutoConnectExecutor
    {
        public List<ZLinkAutoConnectTarget> Connected { get; } = [];

        public List<ZLinkAutoConnectTarget> Disconnected { get; } = [];

        public void Connect(ZLinkAutoConnectTarget target) => Connected.Add(target);

        public void Disconnect(ZLinkAutoConnectTarget target) => Disconnected.Add(target);
    }
}
