using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class LocationResolverTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);
    private static readonly ZLinkActorLocationKey ActorKey = new("actor-1");

    [Fact]
    public async Task Every_Resolve_Reads_The_Store()
    {
        // The store reader has no cache (location runtime contract §8):
        // consecutive resolves of the same key each reach the store, and a
        // takeover behind the caller's back is visible on the very next read.
        var fixture = await FixtureAsync();
        var counting = new CountingActorStore(fixture.Store);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            fixture.Store, fixture.Store, counting, fixture.Store,
            new ZLinkOwnerLeaseTracker(
                fixture.Store, new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero }, fixture.Time),
            observed);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerA, first!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);

        var second = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerB, second!.OwnerId);
        Assert.Equal(2, counting.ResolveCalls);
    }

    [Fact]
    public async Task Spot_List_Uses_The_Configured_PageSize()
    {
        var store = DispatchProxy.Create<IZLinkLocationStore, PageRecordingLocationStore>();
        var recording = (PageRecordingLocationStore)(object)store;
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.AddLocationStore(store);
            options.ConfigureLocations().ListPageSize = 7;
        });
        await using var provider = services.BuildServiceProvider();
        var resolvers = provider.GetRequiredService<ZLinkStoreLocationResolvers>();

        _ = await resolvers.ListLiveSpotRowsAsync(new ZLinkSpotLocationFilter());

        Assert.Equal(7, recording.LastSpotPage?.PageSize);
    }

    [Fact]
    public async Task NotFound_Then_Claim_Is_Visible_Immediately()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // An actor created right after a miss must be visible to the very
        // next resolve — the create-if-absent race depends on it.
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Rows_Of_Expired_Owner_Are_Not_Returned()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // Owner A crashes: no more heartbeats, the lease runs out. The row
        // itself is never written again, yet resolve must stop returning it.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Actor_Row_Is_Not_Returned_Before_ActorRef_Is_Published()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0) with { ActorRef = null },
            ZLinkLocationWriteIntent.NewClaim);

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
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
        var both = await fixture.Resolvers.ListLivePeersAsync(filter);
        Assert.Equal(2, both.Count);

        // Owner A's lease expires; the peer drops out of the desired set
        // within one polling interval without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var survivors = await fixture.Resolvers.ListLivePeersAsync(filter);
        Assert.Single(survivors);
        Assert.Equal(OwnerB, survivors[0].OwnerId);
    }

    [Fact]
    public async Task Route_Rows_Of_Expired_Owner_Are_Not_Returned()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateRouteAsync(
            InMemoryLocationStoreTests.Route(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var key = new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "route-1");
        Assert.NotNull(await fixture.Resolvers.ResolveRouteAsync(key));

        // Owner A crashes: its lease runs out and the route row must stop
        // resolving without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveRouteAsync(key));
    }

    [Fact]
    public async Task Spot_Address_Resolves_Across_Registered_Meshes()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim);

        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "other", new ZLinkSpotMeshChannelRegistration { ChannelName = "other" });
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var spots = new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers);
        var addresses = new ZLinkLocationAddressResolvers(fixture.Resolvers, spots, new ZLinkSpotHandleRegistry());

        var address = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync(RoutingId.From("spot-1")));

        Assert.Equal(RoutingId.From("spot-1"), address.SpotRid);
        Assert.Equal(RoutingId.From("node-1"), address.Snapshot.NodeRid);
        Assert.Equal(ZLinkSpotKind.User, address.Snapshot.SpotKind);

        Assert.Null(await addresses.ResolveSpotHandleAsync(RoutingId.From("no-such-spot")));
    }

    [Fact]
    public async Task Actor_Handle_Internal_Snapshot_Preserves_Entry_Owner_And_Kind()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0, "actor-entry") with
            {
                LocationKind = ZLinkSpotKind.Entry,
                SpotRid = null
            },
            ZLinkLocationWriteIntent.NewClaim);
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var locationOptions = new ZLinkLocationOptions();
        locationOptions.MapSpotMeshToRouteChannel("play", "play.route");
        var routeChannels = new ZLinkSpotRouterChannelMap(locationOptions);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers),
            new ZLinkSpotHandleRegistry(routeChannels),
            routeChannels);

        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("actor-entry"));

        Assert.Equal(RoutingId.From("node-1"), handle.Snapshot.NodeRid);
        Assert.Equal(RoutingId.From("node-1"), handle.SpotRid);
        Assert.Equal(ZLinkSpotKind.Entry, handle.Snapshot.SpotKind);
        Assert.DoesNotContain(
            typeof(SpotHandle).GetProperties(),
            static property => property.Name is "NodeRid" or "SpotKind");
    }

    [Fact]
    public async Task Spot_And_Actor_Handles_Use_Configured_Route_Channel_And_Refresh_Uses_The_Same_Map()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-mapped"),
            ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0, "actor-mapped") with
            {
                SpotRid = RoutingId.From("spot-mapped")
            },
            ZLinkLocationWriteIntent.NewClaim);

        var options = new ZLinkLocationOptions();
        options.MapSpotMeshToRouteChannel("play", "play.route");
        var map = new ZLinkSpotRouterChannelMap(options);
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var handles = new ZLinkSpotHandleRegistry(map);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers),
            handles,
            map);

        var spot = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync(RoutingId.From("spot-mapped")));
        var actor = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("actor-mapped"));
        Assert.Equal("play.route", spot.Snapshot.RouterChannelId);
        Assert.Equal("play.route", actor.Snapshot.RouterChannelId);

        handles.UpdateSpot(InMemoryLocationStoreTests.Spot(OwnerA, "spot-mapped") with
        {
            Generation = 2
        });
        handles.UpdateActor(InMemoryLocationStoreTests.Actor(OwnerA, 2, "actor-mapped") with
        {
            SpotRid = RoutingId.From("spot-mapped")
        });
        Assert.Equal("play.route", spot.Snapshot.RouterChannelId);
        Assert.Equal("play.route", actor.Snapshot.RouterChannelId);
    }

    [Fact]
    public async Task Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found()
    {
        var fixture = await FixtureAsync();
        var initial = InMemoryLocationStoreTests.Spot(OwnerA, "spot-refresh");
        await fixture.Store.UpdateSpotAsync(initial, ZLinkLocationWriteIntent.NewClaim);
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var locationOptions = new ZLinkLocationOptions();
        locationOptions.MapSpotMeshToRouteChannel("play", "play.route");
        var routeChannels = new ZLinkSpotRouterChannelMap(locationOptions);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers),
            new ZLinkSpotHandleRegistry(routeChannels),
            routeChannels);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync(initial.SpotRid));

        await fixture.Store.UpdateSpotAsync(
            initial with { NodeRid = RoutingId.From("node-2"), OwnerId = OwnerB },
            ZLinkLocationWriteIntent.Takeover);
        var attempts = new List<(string RouteChannel, RoutingId NodeRid)>();
        var resolvedNode = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
            handle,
            snapshot =>
            {
                attempts.Add((snapshot.RouterChannelId, snapshot.NodeRid));
                if (attempts.Count == 1)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotRouteNotFound,
                        "stale route");
                return ValueTask.FromResult(snapshot.NodeRid);
            },
            CancellationToken.None);

        Assert.Equal(
            [("play.route", RoutingId.From("node-1")), ("play.route", RoutingId.From("node-2"))],
            attempts);
        Assert.Equal(RoutingId.From("node-2"), resolvedNode);
    }

    [Fact]
    public void Spot_Handle_Registry_Does_Not_Cross_Mesh_Boundaries()
    {
        var spotRid = RoutingId.From("shared-spot");
        var handle = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-1"), spotRid, 1),
            _ => ValueTask.FromResult<ZLinkSpotHandleSnapshot?>(null));
        var handles = new ZLinkSpotHandleRegistry();
        handles.RegisterSpot(new ZLinkSpotLocationKey("play", spotRid), handle);

        handles.UpdateSpot(InMemoryLocationStoreTests.Spot(OwnerB, "shared-spot") with
        {
            MeshName = "other",
            NodeRid = RoutingId.From("node-2"),
            Generation = 2
        });
        handles.RemoveSpot(new ZLinkSpotLocationKey("other", spotRid), 3);

        Assert.Equal("play", handle.Snapshot.RouterChannelId);
        Assert.Equal(RoutingId.From("node-1"), handle.Snapshot.NodeRid);
        Assert.Equal(1, handle.CurrentGeneration);
    }

    [Fact]
    public void Polling_Snapshot_Preserves_Each_Live_Handle_Generation()
    {
        var spotRid = RoutingId.From("shared-spot");
        var key = new ZLinkSpotLocationKey("play", spotRid);
        var first = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-1"), spotRid, 1),
            _ => ValueTask.FromResult<ZLinkSpotHandleSnapshot?>(null));
        var second = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-2"), spotRid, 2),
            _ => ValueTask.FromResult<ZLinkSpotHandleSnapshot?>(null));
        var handles = new ZLinkSpotHandleRegistry();
        handles.RegisterSpot(key, first);
        handles.RegisterSpot(key, second);

        var live = handles.SnapshotLiveKeys().Spots;
        foreach (var entry in live)
            handles.RemoveSpotIfUnchanged(entry.LocationKey, entry.Generation);

        Assert.Equal(new long[] { 1, 2 }, live.Select(static entry => entry.Generation).Order().ToArray());
        Assert.Throws<ZLinkFrameworkException>(() => _ = first.Snapshot);
        Assert.Throws<ZLinkFrameworkException>(() => _ = second.Snapshot);
    }

    [Fact]
    public async Task Watch_Upsert_Does_Not_Apply_A_Row_Older_Than_The_Event()
    {
        var fixture = await FixtureAsync();
        var row = InMemoryLocationStoreTests.Spot(OwnerA, "spot-watch");
        var written = await fixture.Store.UpdateSpotAsync(row, ZLinkLocationWriteIntent.NewClaim);
        var handles = new ZLinkSpotHandleRegistry();
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var resolver = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers),
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await resolver.ResolveSpotHandleAsync(row.SpotRid));
        await using var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            handles,
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMinutes(1) });

        await host.ApplyAsync(
            new ZLinkLocationChanged(
                ZLinkLocationKind.Spot,
                new ZLinkLocationKey.Spot(new ZLinkSpotLocationKey("play", row.SpotRid)),
                ZLinkLocationChangeType.Upserted,
                written.Generation + 1,
                DateTimeOffset.UtcNow),
            CancellationToken.None);

        Assert.Throws<ZLinkFrameworkException>(() => _ = handle.Snapshot);
        Assert.Equal(written.Generation + 1, handle.CurrentGeneration);

        handles.UpdateSpot(row with
        {
            NodeRid = RoutingId.From("node-recovered"),
            Generation = written.Generation + 1
        });

        Assert.Equal(RoutingId.From("node-recovered"), handle.Snapshot.NodeRid);
    }

    [Fact]
    public async Task Watch_Upsert_Preserves_Configured_Route_Channel_Mapping()
    {
        var fixture = await FixtureAsync();
        var initial = InMemoryLocationStoreTests.Spot(OwnerA, "spot-watch-map");
        await fixture.Store.UpdateSpotAsync(initial, ZLinkLocationWriteIntent.NewClaim);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMinutes(1) };
        options.MapSpotMeshToRouteChannel("play", "play.route");
        var routeChannels = new ZLinkSpotRouterChannelMap(options);
        var handles = new ZLinkSpotHandleRegistry(routeChannels);
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotMeshChannels.Add(
            "play", new ZLinkSpotMeshChannelRegistration { ChannelName = "play" });
        var resolver = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers),
            handles,
            routeChannels);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await resolver.ResolveSpotHandleAsync(initial.SpotRid));
        var takeover = await fixture.Store.UpdateSpotAsync(
            initial with { OwnerId = OwnerB, NodeRid = RoutingId.From("node-2") },
            ZLinkLocationWriteIntent.Takeover);
        await using var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            handles,
            options);

        await host.ApplyAsync(
            new ZLinkLocationChanged(
                ZLinkLocationKind.Spot,
                new ZLinkLocationKey.Spot(new ZLinkSpotLocationKey("play", initial.SpotRid)),
                ZLinkLocationChangeType.Upserted,
                takeover.Generation,
                DateTimeOffset.UtcNow),
            CancellationToken.None);

        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal("play.route", handle.Snapshot.RouterChannelId);
    }

    [Fact]
    public async Task Spot_Handle_Request_Does_Not_Refresh_On_Route_Not_Connected()
    {
        var refreshCalls = 0;
        var operationCalls = 0;
        var handle = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-1"),
                RoutingId.From("spot-1"),
                1),
            _ =>
            {
                refreshCalls++;
                return ValueTask.FromResult<ZLinkSpotHandleSnapshot?>(null);
            });

        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(async () =>
            await ZLinkSpotHandleRequestExecution.ExecuteAsync<bool>(
                handle,
                _ =>
                {
                    operationCalls++;
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RouteNotConnected,
                        "route is converging");
                },
                CancellationToken.None));

        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, error.Kind);
        Assert.Equal(1, operationCalls);
        Assert.Equal(0, refreshCalls);
    }

    [Fact]
    public void Spot_Handle_Does_Not_Apply_An_Older_Generation()
    {
        var handle = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-new"),
                RoutingId.From("spot-new"),
                2),
            _ => ValueTask.FromResult<ZLinkSpotHandleSnapshot?>(null));

        handle.Update(new ZLinkSpotHandleSnapshot(
            "play",
            RoutingId.From("node-old"),
            RoutingId.From("spot-old"),
            1));
        handle.Invalidate(1);

        Assert.Equal(RoutingId.From("node-new"), handle.Snapshot.NodeRid);
        Assert.Equal(RoutingId.From("spot-new"), handle.SpotRid);
        Assert.Equal(2, handle.CurrentGeneration);

        handle.Invalidate(3);
        handle.MarkPending(3);
        handle.InvalidateIfGeneration(3);
        handle.Update(new ZLinkSpotHandleSnapshot(
            "play",
            RoutingId.From("node-same"),
            RoutingId.From("spot-same"),
            3));
        handle.Update(new ZLinkSpotHandleSnapshot(
            "play",
            RoutingId.From("node-delayed"),
            RoutingId.From("spot-delayed"),
            2));
        Assert.Throws<ZLinkFrameworkException>(() => _ = handle.Snapshot);
        Assert.Equal(3, handle.CurrentGeneration);
    }

    [Fact]
    public async Task Actor_Address_Is_The_Entry_Spot_For_Entry_Actors_And_The_User_Spot_Otherwise()
    {
        var fixture = await FixtureAsync();
        var registration = new ZLinkFrameworkRegistration();
        var spots = new ZLinkSpotMeshLocationResolver(registration, fixture.Resolvers);
        var addresses = new ZLinkLocationAddressResolvers(fixture.Resolvers, spots, new ZLinkSpotHandleRegistry());

        var entryActor = InMemoryLocationStoreTests.Actor(OwnerA, 0) with
        {
            LocationKind = ZLinkSpotKind.Entry,
            SpotRid = null
        };
        await fixture.Store.UpdateActorAsync(entryActor, ZLinkLocationWriteIntent.NewClaim);

        var entryAddress = await addresses.ResolveActorSpotHandleAsync(entryActor.ActorId);
        Assert.NotNull(entryAddress);
        Assert.Equal(entryActor.NodeRid, entryAddress.SpotRid);

        var userActor = entryActor with
        {
            ActorId = "actor-2",
            LocationKind = ZLinkSpotKind.User,
            SpotRid = RoutingId.From("spot-7")
        };
        await fixture.Store.UpdateActorAsync(userActor, ZLinkLocationWriteIntent.NewClaim);

        var userAddress = await addresses.ResolveActorSpotHandleAsync(userActor.ActorId);
        Assert.NotNull(userAddress);
        Assert.Equal(RoutingId.From("spot-7"), userAddress.SpotRid);
    }

    [Fact]
    public async Task Actor_Spot_Handle_Refreshes_By_Actor_Id_After_The_Actor_Moves()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA, 0) with
        {
            LocationKind = ZLinkSpotKind.User,
            SpotRid = RoutingId.From("spot-old")
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(new ZLinkFrameworkRegistration(), fixture.Resolvers),
            new ZLinkSpotHandleRegistry());
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync(actor.ActorId));

        await fixture.Store.UpdateActorAsync(
            actor with
            {
                OwnerId = OwnerB,
                NodeRid = RoutingId.From("node-2"),
                SpotRid = RoutingId.From("spot-new")
            },
            ZLinkLocationWriteIntent.Takeover);

        var attempts = new List<RoutingId>();
        var spotRid = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
            handle,
            snapshot =>
            {
                attempts.Add(snapshot.SpotRid);
                if (attempts.Count == 1)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RequestTargetNotFound,
                        "actor moved");
                return ValueTask.FromResult(snapshot.SpotRid);
            },
            CancellationToken.None);

        Assert.Equal([RoutingId.From("spot-old"), RoutingId.From("spot-new")], attempts);
        Assert.Equal(RoutingId.From("spot-new"), spotRid);
        Assert.Equal(RoutingId.From("spot-new"), handle.SpotRid);
    }

    [Fact]
    public async Task Location_Event_Updates_Existing_Actor_Spot_Handle_Snapshot()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA, 0) with
        {
            LocationKind = ZLinkSpotKind.User,
            SpotRid = RoutingId.From("spot-old")
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var handles = new ZLinkSpotHandleRegistry();
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotMeshLocationResolver(new ZLinkFrameworkRegistration(), fixture.Resolvers),
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync(actor.ActorId));

        handles.UpdateActor(actor with
        {
            NodeRid = RoutingId.From("node-2"),
            SpotRid = RoutingId.From("spot-new"),
            Generation = handle.CurrentGeneration + 1
        });

        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal(RoutingId.From("spot-new"), handle.SpotRid);
    }

    [Fact]
    public async Task Handle_Polling_Updates_Actor_Snapshot_When_Watch_Is_Unavailable()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA, 0) with
        {
            LocationKind = ZLinkSpotKind.User,
            SpotRid = RoutingId.From("spot-old")
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var locationOptions = new ZLinkLocationOptions
        {
            PollingInterval = TimeSpan.FromMilliseconds(10)
        };
        locationOptions.MapSpotMeshToRouteChannel("play", "play.route");
        var routeChannels = new ZLinkSpotRouterChannelMap(locationOptions);
        var handles = new ZLinkSpotHandleRegistry(routeChannels);
        var spots = new ZLinkSpotMeshLocationResolver(new ZLinkFrameworkRegistration(), fixture.Resolvers);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            spots,
            handles,
            routeChannels);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync(actor.ActorId));
        await using var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            handles,
            locationOptions);
        await host.StartAsync(CancellationToken.None);

        await fixture.Store.UpdateActorAsync(
            actor with
            {
                OwnerId = OwnerB,
                NodeRid = RoutingId.From("node-2"),
                SpotRid = RoutingId.From("spot-new")
            },
            ZLinkLocationWriteIntent.Takeover);

        await WaitUntilAsync(
            () => handle.SpotRid == RoutingId.From("spot-new"),
            TimeSpan.FromSeconds(2));
        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal("play.route", handle.Snapshot.RouterChannelId);
    }

    [Fact]
    public async Task Spot_Handle_Watch_Host_Disposal_Is_Idempotent()
    {
        var fixture = await FixtureAsync();
        var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry(),
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMilliseconds(10) });
        await host.StartAsync(CancellationToken.None);
        await Task.WhenAll(
            host.StopAsync(CancellationToken.None),
            host.DisposeAsync().AsTask(),
            host.DisposeAsync().AsTask());
        await host.DisposeAsync();
    }

    private static async Task WaitUntilAsync(Func<bool> condition, TimeSpan timeout)
    {
        using var cancellation = new CancellationTokenSource(timeout);
        while (!condition())
            await Task.Delay(10, cancellation.Token);
    }

    [Fact]
    public async Task Peer_Rows_Outside_The_Closed_Value_Sets_Are_Ignored()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        await store.UpdatePeerAsync(
            InMemoryLocationStoreTests.Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A store (replica lag, another runtime's bug, hand-edited rows)
        // can serve values outside the closed sets; the resolver must drop
        // them instead of feeding them to reconcile.
        var junk = new JunkAppendingPeerStore(
            store,
            InMemoryLocationStoreTests.Peer(OwnerA, "tcp://junk:1", "node-x") with
            {
                Role = (ZLinkLocationRole)99
            },
            InMemoryLocationStoreTests.Peer(OwnerA, "tcp://junk:2", "node-y") with
            {
                AutoConnectType = (ZLinkLocationAutoConnectType)77
            });
        var resolvers = new ZLinkStoreLocationResolvers(
            junk, store, store, store, tracker, new ZLinkObservedLocationGenerations());

        var rows = await resolvers.ListLivePeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));

        var survivor = Assert.Single(rows);
        Assert.Equal(ZLinkLocationRole.Router, survivor.Role);
    }

    private sealed class JunkAppendingPeerStore(
        IZLinkPeerLocationStore inner,
        params ZLinkPeerLocation[] junk) : IZLinkPeerLocationStore
    {
        public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
            ZLinkPeerLocationFilter filter,
            CancellationToken cancellationToken = default)
        {
            var rows = await inner.ListPeersAsync(filter, cancellationToken);
            return [.. rows, .. junk];
        }

        public ValueTask<ZLinkLocationWriteResult> UpdatePeerAsync(
            ZLinkPeerLocation peer,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdatePeerAsync(peer, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
            ZLinkPeerLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemovePeerAsync(key, owner, cancellationToken);

    }

    [Fact]
    public async Task Actor_Type_Null_And_Empty_Are_The_Same_Key()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, 0) with { ActorType = "" },
            ZLinkLocationWriteIntent.NewClaim);

        var byEmpty = await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("actor-1"));
        var byNull = await fixture.Resolvers.ResolveActorRowAsync(new ZLinkActorLocationKey("actor-1"));

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
            store, store, replica, store, tracker, new ZLinkObservedLocationGenerations());

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(2, first!.Generation);

        // The lagging read must not roll the observed view backwards.
        Assert.Null(await resolvers.ResolveActorRowAsync(ActorKey));

        // An equal generation is accepted again.
        var third = await resolvers.ResolveActorRowAsync(ActorKey);
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

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }

    private sealed class CountingActorStore(IZLinkActorLocationStore inner) : IZLinkActorLocationStore
    {
        public int ResolveCalls { get; private set; }

        public ValueTask<ZLinkActorLocation?> ResolveActorAsync(
            ZLinkActorLocationKey key,
            CancellationToken cancellationToken = default)
        {
            ResolveCalls++;
            return inner.ResolveActorAsync(key, cancellationToken);
        }

        public ValueTask<ZLinkLocationWriteResult> UpdateActorAsync(
            ZLinkActorLocation actor,
            ZLinkLocationWriteIntent intent,
            CancellationToken cancellationToken = default) =>
            inner.UpdateActorAsync(actor, intent, cancellationToken);

        public ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveActorAsync(key, owner, cancellationToken);

        public ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
            ZLinkActorLocationFilter filter,
            ZLinkPageRequest page = default,
            CancellationToken cancellationToken = default) =>
            inner.ListActorsAsync(filter, page, cancellationToken);
    }

    private class PageRecordingLocationStore : DispatchProxy
    {
        private readonly ZLinkInMemoryLocationStore _inner = new();

        public ZLinkPageRequest? LastSpotPage { get; private set; }

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            Assert.NotNull(targetMethod);
            Assert.NotNull(args);
            if (targetMethod.Name == nameof(IZLinkSpotLocationStore.ListSpotsAsync))
                LastSpotPage = Assert.IsType<ZLinkPageRequest>(args[1]);

            return targetMethod.Invoke(_inner, args);
        }
    }

    private static async Task<ResolverFixture> FixtureAsync()
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

        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, store, store, tracker, observed);
        return new ResolverFixture(store, resolvers, time);
    }

    private sealed record ResolverFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ManualTimeProvider Time);
}
