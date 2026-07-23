using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class AutoConnectReconcilerTests
{
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

    [Fact]
    public void Planner_Excludes_Self_And_Foreign_Meshes()
    {
        var local = Local(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Dealer, "local", "tcp://l:1");
        var descriptors = new[]
        {
            Descriptor("r1", "tcp://r:1"),
            // Same rid as local: excluded as self.
            Descriptor("local", "tcp://r:2"),
            // Different mesh: ignored.
            Descriptor("r2", "tcp://r:3", mesh: "other")
        };

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(local, descriptors);

        var target = Assert.Single(desired.Values);
        Assert.Equal("tcp://r:1", target.Endpoint);
        Assert.Equal(1, ZLinkAutoConnectPlanner.CountDiscoveredPeers(local, descriptors));
    }

    [Fact]
    public void Planner_Marks_Draining_Descriptors_Instead_Of_Dropping_Them()
    {
        var local = Local(
            ZLinkLocationAutoConnectType.ClientServer,
            ZLinkLocationRole.Dealer,
            "local",
            "tcp://l:1");
        var draining = Descriptor("remote", "tcp://r:1") with
        {
            State = ZLinkFrameworkRuntimeState.Draining
        };

        var target = Assert.Single(ZLinkAutoConnectPlanner.ComputeDesired(local, [draining])).Value;

        // A draining node stays in the desired set so an already-active
        // connection is not cut; the reconciler skips it for new dials.
        Assert.Equal("tcp://r:1", target.Endpoint);
        Assert.True(target.Draining);
    }

    [Fact]
    public async Task Draining_Descriptor_Is_Not_Selected_For_A_New_Connection()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1", draining: true);

        await fixture.Reconciler.TickAsync();

        Assert.Empty(fixture.Executor.Connected);
    }

    [Fact]
    public async Task Draining_Marker_Is_Monotonic_Across_Subsequent_Renewal()
    {
        var fixture = await FixtureAsync();
        await fixture.Reconciler.TickAsync();

        Assert.True(await fixture.Reconciler.MarkDrainingAsync());
        await fixture.Reconciler.TickAsync();

        var row = Assert.Single(
            await fixture.Store.ListMeshNodesAsync("play"),
            row => row.Rid.Equals(RoutingId.From("local")));
        Assert.Equal(ZLinkFrameworkRuntimeState.Draining, row.State);
        Assert.True(row.DescriptorRevision > 1);
    }

    [Fact]
    public async Task Runtime_weight_change_renews_the_existing_local_row()
    {
        var fixture = await FixtureAsync();
        await fixture.Reconciler.TickAsync();

        fixture.Reconciler.SetLocalWeight(0);
        await fixture.Reconciler.TickAsync();

        var row = Assert.Single(
            await fixture.Store.ListMeshNodesAsync("play"),
            row => row.Rid.Equals(RoutingId.From("local")));
        Assert.Equal(0, row.ChannelWeights["play"]);
        Assert.Equal(fixture.Runtime.OwnerId, row.OwnerId);
    }

    [Theory]
    [InlineData(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer)]
    [InlineData(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router)]
    public void Symmetric_Mesh_Pairwise_Initiator_Connects_From_The_Smaller_Side_Only(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role)
    {
        var smaller = Local(type, role, "aa", "tcp://a:1");
        var bigger = Local(type, role, "bb", "tcp://b:1");
        var rowSmaller = Descriptor("aa", "tcp://a:1");
        var rowBigger = Descriptor("bb", "tcp://b:1");

        var fromSmaller = ZLinkAutoConnectPlanner.ComputeDesired(smaller, [rowBigger]);
        var fromBigger = ZLinkAutoConnectPlanner.ComputeDesired(bigger, [rowSmaller]);

        // Only the byte-order smaller routing id dials, so route and dealer
        // meshes share one physical link per peer pair.
        Assert.Single(fromSmaller);
        Assert.Empty(fromBigger);
    }

    [Fact]
    public void SpotMesh_All_Members_Dial_While_Only_The_Smaller_Member_Dials_The_Router()
    {
        var smaller = Local(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot, "aa", "tcp://a:1");
        var bigger = Local(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot, "bb", "tcp://b:1");
        var rowSmaller = Descriptor("aa", "tcp://a:1");
        var rowBigger = Descriptor("bb", "tcp://b:1");

        var fromSmaller = Assert.Single(ZLinkAutoConnectPlanner.ComputeDesired(smaller, [rowBigger])).Value;
        var fromBigger = Assert.Single(ZLinkAutoConnectPlanner.ComputeDesired(bigger, [rowSmaller])).Value;

        Assert.True(fromSmaller.InitiatesSpotRouterLink);
        Assert.False(fromBigger.InitiatesSpotRouterLink);
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
        var server = Descriptor("aa", "tcp://a:1");

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

        await fixture.RemovePeerAsync("r1");
        await fixture.Reconciler.TickAsync();

        var disconnected = Assert.Single(fixture.Executor.Disconnected);
        Assert.Equal("tcp://r:1", disconnected.Endpoint);
        Assert.Single(fixture.Reconciler.ActiveTargets);
    }

    [Fact]
    public async Task Failed_Connect_Is_Not_Marked_Active_And_Retries_On_The_Next_Tick()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        fixture.Executor.ConnectSucceeds = false;

        await fixture.Reconciler.TickAsync();
        await fixture.Reconciler.TickAsync();

        Assert.Equal(2, fixture.Executor.Connected.Count);
        Assert.Empty(fixture.Reconciler.ActiveTargets);

        fixture.Executor.ConnectSucceeds = true;
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Reconciler.ActiveTargets);
    }

    [Fact]
    public async Task Store_Failure_Retries_The_Last_Desired_Target_Only_Within_Grace()
    {
        var fixture = await FixtureAsync(options =>
            options.StoreFailureGrace = TimeSpan.FromSeconds(3));
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        fixture.Executor.ConnectSucceeds = false;
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Executor.Connected);

        fixture.PeerResolver.Fail = true;
        await fixture.Reconciler.TickAsync();
        Assert.Equal(2, fixture.Executor.Connected.Count);

        fixture.Time.Advance(TimeSpan.FromSeconds(4));
        await fixture.Reconciler.TickAsync();
        Assert.Equal(2, fixture.Executor.Connected.Count);
    }

    [Fact]
    public async Task Endpoint_Change_For_The_Same_Peer_Key_Is_A_Handover()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        Assert.Equal(
            ZLinkOwnerLeaseReleaseResult.Released,
            await fixture.Store.ReleaseOwnerLeaseAsync(
                new ZLinkLocationOwnerToken("peer-owner", 2)));
        await fixture.Store.RenewOwnerLeaseAsync(
            "peer-owner",
            RoutingId.From("peer-node"),
            TimeSpan.FromMinutes(10));
        await fixture.Store.UpdateMeshNodeAsync(
            Descriptor("r1", "tcp://r:9") with
            {
                LifecycleGeneration = 2,
                DescriptorRevision = 1,
                LeaseGeneration = 3
            },
            ZLinkLocationWriteIntent.Takeover);
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

        // A successful snapshot replaces fail-static history. Once the row is
        // removed, the rid is an unknown request target rather than a known but
        // disconnected route.
        fixture.PeerResolver.Fail = false;
        await fixture.RemovePeerAsync("r1");
        await fixture.Reconciler.TickAsync();
        Assert.False(fixture.Reconciler.KnowsPeer(RoutingId.From("r1")));
    }

    [Fact]
    public async Task Manual_Mesh_Retains_Observed_Target_After_Row_Removal()
    {
        var fixture = await FixtureAsync(retainRemovedMembers: true);
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        await fixture.RemovePeerAsync("r1");
        await fixture.Reconciler.TickAsync();

        Assert.False(fixture.Reconciler.KnowsPeer(RoutingId.From("r1")));
        Assert.True(fixture.Reconciler.HasRetainedPeer(RoutingId.From("r1")));
        Assert.False(fixture.Reconciler.HasRetainedPeer(RoutingId.From("ghost")));
    }

    [Fact]
    public void Membership_Includes_Peers_The_Initiator_Rule_Excludes_From_Dialing()
    {
        // "zz" dials "aa" never (aa initiates), yet aa is a reachable
        // rid-addressed target once it dials us: membership, not the
        // desired dial set, is the fail-fast knowledge source.
        var local = Local(ZLinkLocationAutoConnectType.RouteMesh, ZLinkLocationRole.Router, "zz", "tcp://z:1");
        var descriptor = Descriptor("aa", "tcp://a:1");

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(local, [descriptor]);

        Assert.Empty(desired);
    }

    [Fact]
    public void SpotMesh_Target_Keeps_Peer_RoutingId_For_RidAware_Connect()
    {
        var local = Local(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot, "aa", "tcp://a:1");
        var descriptor = Descriptor("zz", "tcp://z:1");

        var desired = ZLinkAutoConnectPlanner.ComputeDesired(local, [descriptor]);

        var target = Assert.Single(desired.Values);
        Assert.Equal(RoutingId.From("zz"), target.NodeRid);
        Assert.Equal("tcp://z:1", target.Endpoint);
    }

    [Fact]
    public async Task Owner_Change_At_The_Same_Endpoint_Refreshes_Without_A_Second_Dial()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();

        // The peer restarts: same rid and endpoint, re-claimed by a new
        // owner. The socket transport has already reconnected the broken
        // endpoint, so the reconciler must not race it with another
        // disconnect/connect pair.
        Assert.Equal(
            ZLinkOwnerLeaseReleaseResult.Released,
            await fixture.Store.ReleaseOwnerLeaseAsync(
                new ZLinkLocationOwnerToken("peer-owner", 2)));
        await fixture.Store.RenewOwnerLeaseAsync(
            "peer-owner-2", RoutingId.From("peer-node-2"), TimeSpan.FromMinutes(10));
        var restarted = Descriptor("r1", "tcp://r:1") with
        {
            OwnerId = "peer-owner-2",
            LeaseGeneration = 3
        };
        await fixture.Store.UpdateMeshNodeAsync(restarted, ZLinkLocationWriteIntent.Takeover);
        await fixture.Reconciler.TickAsync();

        Assert.Equal("tcp://r:1", Assert.Single(fixture.Executor.Connected).Endpoint);
        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Equal("peer-owner-2", Assert.Single(fixture.Reconciler.ActiveTargets).OwnerId);
    }

    [Fact]
    public async Task Local_Row_Publish_Takes_Over_Same_Key_After_Process_Restart()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.RenewOwnerLeaseAsync(
            "old-local-owner",
            RoutingId.From("old-local-node"),
            LeaseTtl);
        await fixture.Store.UpdateMeshNodeAsync(
            Descriptor("local", "tcp://l:1") with
            {
                OwnerId = "old-local-owner",
                LeaseGeneration = 3
            },
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(
            ZLinkOwnerLeaseReleaseResult.Released,
            await fixture.Store.ReleaseOwnerLeaseAsync(
                new ZLinkLocationOwnerToken("old-local-owner", 3)));

        await fixture.Reconciler.TickAsync();

        var row = Assert.Single(
            await fixture.Store.ListMeshNodesAsync("play"),
            row => row.Rid.Equals(RoutingId.From("local")));
        Assert.Equal(fixture.Runtime.OwnerId, row.OwnerId);
        // The row keeps the writer's core lifecycle generation verbatim;
        // takeover fencing advances the store's owner token, not row content.
        Assert.Equal(
            Descriptor("local", "tcp://l:1").LifecycleGeneration,
            row.LifecycleGeneration);
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
        await fixture.RemovePeerAsync("r1");
        fixture.PeerResolver.Fail = false;
        await fixture.Reconciler.TickAsync();
        Assert.Empty(fixture.Executor.Disconnected);

        // After the grace the fresh list wins and the vanished peer drops.
        fixture.Time.Advance(TimeSpan.FromSeconds(11));
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Executor.Disconnected);
    }

    [Fact]
    public async Task Unhealthy_Owner_Lease_Blocks_A_Successful_Empty_List_From_Disconnecting()
    {
        var fixture = await FixtureAsync();
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Reconciler.ActiveTargets);

        // A store command that began before an outage can complete after the
        // store resumes. The owner heartbeat remains the recovery authority,
        // so an empty list observed while that lease is unhealthy cannot cut
        // an already admitted transport.
        await fixture.Runtime.StartAsync(RoutingId.From("runtime-node"));
        await fixture.Runtime.StopAsync();
        await fixture.RemovePeerAsync("r1");
        await fixture.Reconciler.TickAsync();

        Assert.True(fixture.Reconciler.StoreFailed);
        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Single(fixture.Reconciler.ActiveTargets);
    }

    [Fact]
    public async Task Hung_AutoConnect_Read_Enters_FailStatic_At_The_Lease_Renew_Bound()
    {
        var fixture = await FixtureAsync(options =>
            options.OwnerLeaseRenewTimeout = TimeSpan.FromMilliseconds(25));
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        Assert.Single(fixture.Reconciler.ActiveTargets);

        fixture.PeerResolver.HangUntilCancelled = true;
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        await fixture.Reconciler.TickAsync();
        elapsed.Stop();

        Assert.True(elapsed.Elapsed < TimeSpan.FromSeconds(1));
        Assert.True(fixture.Reconciler.StoreFailed);
        Assert.Empty(fixture.Executor.Disconnected);
        Assert.Single(fixture.Reconciler.ActiveTargets);
    }

    [Fact]
    public async Task Requested_Cancellation_Is_Not_Classified_As_A_Store_Outage()
    {
        var fixture = await FixtureAsync();
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
            await fixture.Reconciler.TickAsync(cancellation.Token));

        Assert.False(fixture.Reconciler.StoreFailed);
    }

    [Fact]
    public async Task StoreFailureGrace_Keeps_Ready_Connections_And_Blocks_New_Outbound_During_Outage()
    {
        var fixture = await FixtureAsync(options => options.StoreFailureGrace = TimeSpan.FromSeconds(3));
        await fixture.PublishPeerAsync("r1", "tcp://r:1");
        await fixture.Reconciler.TickAsync();
        Assert.Equal("tcp://r:1", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);

        fixture.PeerResolver.Fail = true;
        await fixture.RemovePeerAsync("r1");
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

        fixture.Time.Advance(TimeSpan.FromSeconds(11));
        await fixture.Reconciler.TickAsync();

        Assert.Equal("tcp://r:1", Assert.Single(fixture.Executor.Disconnected).Endpoint);
        Assert.Equal("tcp://r:2", Assert.Single(fixture.Reconciler.ActiveTargets).Endpoint);
    }

    [Fact]
    public async Task Recovery_Republishes_The_Local_Row_Before_Reading_The_List()
    {
        var fixture = await FixtureAsync();
        await fixture.Reconciler.TickAsync();
        var published = await fixture.Store.ListMeshNodesAsync("play");
        Assert.Single(published);

        // Outage long enough for the local lease to expire and the row to
        // be claimable again.
        fixture.PeerResolver.Fail = true;
        await fixture.Reconciler.TickAsync();
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));
        await fixture.Runtime.RenewOwnerLeaseOnceAsync();

        fixture.PeerResolver.Fail = false;
        await fixture.Reconciler.TickAsync();

        published = await fixture.Store.ListMeshNodesAsync("play");
        Assert.Single(published);
        Assert.Equal(fixture.Runtime.OwnerId, published[0].OwnerId);
    }

    [Fact]
    public async Task Dial_Only_Capability_Without_Identity_Still_Connects_And_Never_Advertises()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("peer-node"), TimeSpan.FromMinutes(10));
        await store.UpdateMeshNodeAsync(
            Descriptor("r1", "tcp://r:1"),
            ZLinkLocationWriteIntent.NewClaim);

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, tracker, new ZLinkObservedLocationGenerations());
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
        var rows = await store.ListMeshNodesAsync("play");
        Assert.Single(rows);

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

    private static ZLinkMeshNodeDescriptor Descriptor(
        string rid,
        string endpoint,
        string mesh = "play") => new(
        mesh,
        RoutingId.From(rid),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        endpoint,
        new Dictionary<string, int>(StringComparer.Ordinal) { [mesh] = 100 },
        SecurityIdentity: string.Empty,
        OwnerId: "peer-owner",
        LeaseGeneration: 2,
        UpdatedAt: default)
    {
        State = ZLinkFrameworkRuntimeState.Serving
    };

    private static async Task<ReconcilerFixture> FixtureAsync(
        Action<ZLinkLocationOptions>? configure = null,
        bool retainRemovedMembers = false)
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        configure?.Invoke(options);
        var runtime = new ZLinkLocationRuntime(options, store, store, store, store, store, time);
        await runtime.RenewOwnerLeaseOnceAsync();
        await store.RenewOwnerLeaseAsync("peer-owner", RoutingId.From("peer-node"), TimeSpan.FromMinutes(10));

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, tracker, new ZLinkObservedLocationGenerations());
        var failable = new FailablePeerResolver(resolvers);
        var executor = new RecordingExecutor();
        var local = new ZLinkAutoConnectLocal(
            ZLinkLocationAutoConnectType.ClientServer, "play", ZLinkLocationRole.Dealer,
            RoutingId.From("local"), "tcp://l:1");
        var localRow = Descriptor("local", "tcp://l:1") with { OwnerId = "ignored" };
        var reconciler = new ZLinkAutoConnectReconciler(
            local, localRow, runtime, failable, executor, options, time,
            retainRemovedMembers: retainRemovedMembers);
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
        private readonly Dictionary<string, ulong> _generations = new(StringComparer.Ordinal);

        public async Task PublishPeerAsync(
            string rid,
            string endpoint,
            bool takeover = false,
            bool draining = false)
        {
            var row = Descriptor(rid, endpoint) with
            {
                State = draining
                    ? ZLinkFrameworkRuntimeState.Draining
                    : ZLinkFrameworkRuntimeState.Serving
            };
            var result = await Store.UpdateMeshNodeAsync(
                row,
                takeover ? ZLinkLocationWriteIntent.Takeover : ZLinkLocationWriteIntent.NewClaim);
            _generations[rid] = result.Generation;
        }

        public async Task RemovePeerAsync(string rid)
        {
            await Store.RemoveMeshNodeAsync(
                new ZLinkMeshNodeDescriptorKey("play", RoutingId.From(rid)),
                new ZLinkLocationOwnerToken("peer-owner", _generations[rid]));
        }
    }

    private sealed class FailablePeerResolver(IZLinkMeshNodeLocationResolver inner)
        : IZLinkMeshNodeLocationResolver
    {
        public bool Fail { get; set; }

        public bool HangUntilCancelled { get; set; }

        public async ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListLiveMeshNodesAsync(
            string meshName,
            CancellationToken cancellationToken = default)
        {
            if (Fail) throw new InvalidOperationException("store unreachable");
            if (HangUntilCancelled)
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
            return await inner.ListLiveMeshNodesAsync(meshName, cancellationToken);
        }
    }

    private sealed class RecordingExecutor : IZLinkAutoConnectExecutor
    {
        public List<ZLinkAutoConnectTarget> Connected { get; } = [];

        public List<ZLinkAutoConnectTarget> Disconnected { get; } = [];

        public bool ConnectSucceeds { get; set; } = true;

        public bool DisconnectSucceeds { get; set; } = true;

        public bool Connect(ZLinkAutoConnectTarget target) { Connected.Add(target); return ConnectSucceeds; }

        public bool Disconnect(ZLinkAutoConnectTarget target) { Disconnected.Add(target); return DisconnectSucceeds; }
    }
}
