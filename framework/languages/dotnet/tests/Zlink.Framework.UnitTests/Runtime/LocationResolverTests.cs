using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class LocationResolverTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);
    private static readonly ZLinkActorLocationKey ActorKey = new("play", "actor-1");

    [Fact]
    public async Task Every_Resolve_Reads_The_Store()
    {
        // The store reader has no cache: consecutive resolves of the same
        // key each reach the store, and a takeover behind the caller's back
        // is visible on the very next read.
        var fixture = await FixtureAsync();
        var counting = new CountingActorStore(fixture.Store);
        var observed = new ZLinkObservedLocationGenerations();
        var resolvers = new ZLinkStoreLocationResolvers(
            fixture.Store, fixture.Store, counting,
            new ZLinkOwnerLeaseTracker(
                fixture.Store, new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero }, fixture.Time),
            observed);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerA, first!.OwnerId);

        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerB), ZLinkLocationWriteIntent.Takeover);

        var second = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerB, second!.OwnerId);
        Assert.Equal(2, counting.ResolveCalls);
    }

    [Fact]
    public async Task NotFound_Then_Claim_Is_Visible_Immediately()
    {
        var fixture = await FixtureAsync();

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // An actor created right after a miss must be visible to the very
        // next resolve — the create-if-absent race depends on it.
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Rows_Of_Expired_Owner_Are_Not_Returned()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.NotNull(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        // Owner A crashes: no more heartbeats, the lease runs out. The row
        // itself is never written again, yet resolve must stop returning it.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task Actor_Recreated_After_Expired_Owner_Can_Restart_Membership_Epoch()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA) with
            {
                ActorRef = new ActorRef(RoutingId.From("node-1"), "actor-1", 10),
                MembershipEpoch = 2
            },
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(2UL, (await fixture.Resolvers.ResolveActorRowAsync(ActorKey))!.MembershipEpoch);

        // A crashed owner's stale row can remain in storage after its lease
        // expires. That is a confirmed lifecycle end even though the raw row
        // still exists, so the replacement owner may restart the per-instance
        // membership epoch without being mistaken for a lagging replica.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));
        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));

        var replacement = InMemoryLocationStoreTests.Actor(OwnerB) with
        {
            ActorRef = new ActorRef(RoutingId.From("node-2"), "actor-1", 20),
            OwnerNodeRid = RoutingId.From("node-2"),
            MembershipEpoch = 0
        };
        var claim = await fixture.Store.UpdateActorAsync(
            replacement, ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claim.Status);

        var resolved = await fixture.Resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(OwnerB, resolved!.OwnerId);
        Assert.Equal(0UL, resolved.MembershipEpoch);
        Assert.Equal(20UL, resolved.ActorRef.Generation);
    }

    [Fact]
    public async Task Spot_Recreated_After_Expired_Owner_Can_Restart_Generation()
    {
        var fixture = await FixtureAsync();
        const string spotId = "spot-recreated";
        var key = new ZLinkSpotLocationKey(spotId);
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-recreated") with
            {
                SpotGeneration = 10
            },
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(10UL, (await fixture.Resolvers.ResolveSpotRowAsync(key))!.SpotGeneration);

        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));
        Assert.Null(await fixture.Resolvers.ResolveSpotRowAsync(key));

        var claim = await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerB, "spot-recreated") with
            {
                OwnerNodeRid = RoutingId.From("node-2"),
                SpotGeneration = 1
            },
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claim.Status);

        var resolved = await fixture.Resolvers.ResolveSpotRowAsync(key);
        Assert.Equal(OwnerB, resolved!.OwnerId);
        Assert.Equal(1UL, resolved.SpotGeneration);
    }

    [Fact]
    public async Task Actor_Row_Is_Not_Returned_Before_The_Reference_Generation_Is_Published()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA) with
            {
                ActorRef = new ActorRef(RoutingId.From("node-1"), "actor-1", 0)
            },
            ZLinkLocationWriteIntent.NewClaim);

        Assert.Null(await fixture.Resolvers.ResolveActorRowAsync(ActorKey));
    }

    [Fact]
    public async Task MeshNode_List_Excludes_Expired_Owners()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateMeshNodeAsync(
            InMemoryLocationStoreTests.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));
        await fixture.Store.UpdateMeshNodeAsync(
            InMemoryLocationStoreTests.MeshNode(
                OwnerB,
                "tcp://127.0.0.1:5002",
                "node-2",
                leaseGeneration: 2),
            ZLinkLocationWriteIntent.NewClaim);

        var both = await fixture.Resolvers.ListLiveMeshNodesAsync("play");
        Assert.Equal(2, both.Count);

        // Owner A's lease expires; the descriptor drops out of the desired
        // set within one polling interval without any row write.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var survivors = await fixture.Resolvers.ListLiveMeshNodesAsync("play");
        Assert.Single(survivors);
        Assert.Equal(OwnerB, survivors[0].OwnerId);
    }

    [Fact]
    public async Task MeshNode_Successor_Owner_Can_Restart_Generation_Without_An_Observed_Miss()
    {
        var fixture = await FixtureAsync();
        var predecessor = InMemoryLocationStoreTests.MeshNode(OwnerA) with
        {
            LifecycleGeneration = 20,
            DescriptorRevision = 4
        };
        await fixture.Store.UpdateMeshNodeAsync(
            predecessor, ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(
            20UL,
            Assert.Single(await fixture.Resolvers.ListLiveMeshNodesAsync("play"))
                .LifecycleGeneration);

        // The store takeover replaces the row atomically. A polling reader can
        // therefore see the new owner without ever observing a missing key.
        // Core generations are process-local wall-clock values, so the new
        // owner may legitimately start below the predecessor's value.
        fixture.Time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));
        await fixture.Store.RenewOwnerLeaseAsync(
            OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));
        var successor = InMemoryLocationStoreTests.MeshNode(
            OwnerB,
            "tcp://127.0.0.1:5002",
            "node-1",
            leaseGeneration: 2) with
        {
            LifecycleGeneration = 10,
            DescriptorRevision = 1
        };
        var takeover = await fixture.Store.UpdateMeshNodeAsync(
            successor, ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);

        var resolved = Assert.Single(await fixture.Resolvers.ListLiveMeshNodesAsync("play"));
        Assert.Equal(OwnerB, resolved.OwnerId);
        Assert.Equal(10UL, resolved.LifecycleGeneration);

        // Once the successor is accepted, a delayed predecessor row must not
        // move the observed incarnation back to its retired owner.
        var observed = new ZLinkObservedLocationGenerations();
        Assert.True(observed.AcceptDescriptor(predecessor));
        Assert.True(observed.AcceptDescriptor(successor));
        Assert.False(observed.AcceptDescriptor(predecessor));
    }

    [Fact]
    public async Task Spot_Address_Uses_Global_Id_And_Returns_The_Canonical_Row_Mesh()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim);

        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());

        var address = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync("play", "spot-1"));

        Assert.Equal("spot-1", address.SpotId);
        Assert.Equal(RoutingId.From("node-1"), address.Snapshot.NodeRid);
        Assert.Equal(ZLinkSpotKind.User, address.Snapshot.SpotKind);

        var sameGlobalAddress = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync("other", "spot-1"));
        Assert.Equal("play", sameGlobalAddress.MeshName);
    }

    [Fact]
    public async Task Spot_Address_Uses_The_Canonical_Row_Mesh_For_A_Global_Id()
    {
        var fixture = await FixtureAsync();
        const string sharedRid = "shared-entry";
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "shared-entry") with
            {
                MeshName = "external",
                SpotId = sharedRid,
                OwnerNodeRid = RoutingId.From("node-1")
            },
            ZLinkLocationWriteIntent.NewClaim);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());

        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync("play", sharedRid));

        Assert.Equal("external", handle.MeshName);
        Assert.Equal(RoutingId.From("node-1"), handle.Snapshot.NodeRid);
    }

    [Fact]
    public async Task Actor_Handle_Internal_Snapshot_Preserves_Entry_Owner_And_Kind()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, "actor-entry"),
            ZLinkLocationWriteIntent.NewClaim);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());

        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", "actor-entry"));

        Assert.Equal(RoutingId.From("node-1"), handle.Snapshot.NodeRid);
        Assert.Equal(handle.Snapshot.SpotId, handle.SpotId);
        Assert.Equal(ZLinkSpotKind.Entry, handle.Snapshot.SpotKind);
        Assert.DoesNotContain(
            typeof(SpotHandle).GetProperties(),
            static property => property.Name is "NodeRid" or "SpotKind");
    }

    [Fact]
    public async Task Actor_Handles_With_The_Same_Id_Are_Isolated_By_MeshName()
    {
        var fixture = await FixtureAsync();
        var play = InMemoryLocationStoreTests.Actor(OwnerA, "shared-actor") with
        {
            SpotKind = ZLinkSpotKind.User,
            SpotId = "play-spot"
        };
        var external = InMemoryLocationStoreTests.Actor(OwnerB, "shared-actor") with
        {
            MeshName = "external",
            ActorRef = new ActorRef(RoutingId.From("node-2"), "shared-actor", 1),
            OwnerNodeRid = RoutingId.From("node-2"),
            SpotKind = ZLinkSpotKind.User,
            SpotId = "external-spot"
        };
        await fixture.Store.UpdateActorAsync(play, ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.UpdateActorAsync(external, ZLinkLocationWriteIntent.NewClaim);
        var handles = new ZLinkSpotHandleRegistry();
        var addresses = new ZLinkLocationAddressResolvers(fixture.Resolvers, handles);

        var playHandle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", "shared-actor"));
        var externalHandle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("external", "shared-actor"));

        handles.UpdateActor(external with
        {
            SpotId = "external-moved",
            MembershipEpoch = external.MembershipEpoch + 1
        });

        Assert.Equal("play-spot", playHandle.SpotId);
        Assert.Equal("external-moved", externalHandle.SpotId);
    }

    [Fact]
    public async Task Spot_And_Actor_Handles_Preserve_The_Row_MeshName_Across_Refresh()
    {
        var fixture = await FixtureAsync();
        await fixture.Store.UpdateSpotAsync(
            InMemoryLocationStoreTests.Spot(OwnerA, "spot-mapped"),
            ZLinkLocationWriteIntent.NewClaim);
        await fixture.Store.UpdateActorAsync(
            InMemoryLocationStoreTests.Actor(OwnerA, "actor-mapped") with
            {
                SpotKind = ZLinkSpotKind.User,
                SpotId = "spot-mapped"
            },
            ZLinkLocationWriteIntent.NewClaim);

        var handles = new ZLinkSpotHandleRegistry();
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            handles);

        var spot = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync("play", "spot-mapped"));
        var actor = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", "actor-mapped"));
        Assert.Equal("play", spot.Snapshot.RouterChannelId);
        Assert.Equal("play", actor.Snapshot.RouterChannelId);

        handles.UpdateSpot(InMemoryLocationStoreTests.Spot(OwnerA, "spot-mapped") with
        {
            SpotGeneration = 2
        });
        handles.UpdateActor(InMemoryLocationStoreTests.Actor(OwnerA, "actor-mapped") with
        {
            SpotKind = ZLinkSpotKind.User,
            SpotId = "spot-mapped",
            MembershipEpoch = 2
        });
        Assert.Equal("play", spot.Snapshot.RouterChannelId);
        Assert.Equal("play", actor.Snapshot.RouterChannelId);
    }

    [Fact]
    public async Task Spot_Handle_Request_Refreshes_Once_After_Target_Not_Found()
    {
        var fixture = await FixtureAsync();
        var initial = InMemoryLocationStoreTests.Spot(OwnerA, "spot-refresh");
        await fixture.Store.UpdateSpotAsync(initial, ZLinkLocationWriteIntent.NewClaim);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveSpotHandleAsync("play", initial.SpotId));

        await fixture.Store.UpdateSpotAsync(
            initial with { OwnerNodeRid = RoutingId.From("node-2"), OwnerId = OwnerB },
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
            [("play", RoutingId.From("node-1")), ("play", RoutingId.From("node-2"))],
            attempts);
        Assert.Equal(RoutingId.From("node-2"), resolvedNode);
    }

    [Fact]
    public void Spot_Handle_Registry_Uses_Global_SpotId_Across_Mesh_Labels()
    {
        const string spotId = "shared-spot";
        var handle = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-1"), spotId, 1),
            1,
            _ => ValueTask.FromResult<(ZLinkSpotHandleSnapshot, ulong)?>(null));
        var handles = new ZLinkSpotHandleRegistry();
        handles.RegisterSpot(new ZLinkSpotLocationKey(spotId), handle);

        handles.UpdateSpot(InMemoryLocationStoreTests.Spot(OwnerB, "shared-spot") with
        {
            MeshName = "other",
            OwnerNodeRid = RoutingId.From("node-2"),
            SpotGeneration = 2
        });
        handles.RemoveSpot(new ZLinkSpotLocationKey(spotId), 3);

        Assert.Throws<ZLinkFrameworkException>(() => _ = handle.Snapshot);
    }

    [Fact]
    public void Polling_Refresh_Invalidates_Handles_Whose_Row_Vanished()
    {
        const string spotId = "shared-spot";
        var key = new ZLinkSpotLocationKey(spotId);
        var first = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-1"), spotId, 1),
            1,
            _ => ValueTask.FromResult<(ZLinkSpotHandleSnapshot, ulong)?>(null));
        var second = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot("play", RoutingId.From("node-2"), spotId, 2),
            2,
            _ => ValueTask.FromResult<(ZLinkSpotHandleSnapshot, ulong)?>(null));
        var handles = new ZLinkSpotHandleRegistry();
        handles.RegisterSpot(key, first);
        handles.RegisterSpot(key, second);

        foreach (var handle in handles.SnapshotLiveHandles())
            handle.InvalidateCurrent();

        Assert.Throws<ZLinkFrameworkException>(() => _ = first.Snapshot);
        Assert.Throws<ZLinkFrameworkException>(() => _ = second.Snapshot);
    }

    [Fact]
    public async Task Watch_Upsert_Applies_The_Current_Row_And_Preserves_The_MeshName()
    {
        var fixture = await FixtureAsync();
        var initial = InMemoryLocationStoreTests.Spot(OwnerA, "spot-watch-map");
        await fixture.Store.UpdateSpotAsync(initial, ZLinkLocationWriteIntent.NewClaim);
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMinutes(1) };
        var handles = new ZLinkSpotHandleRegistry();
        var resolver = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await resolver.ResolveSpotHandleAsync("play", initial.SpotId));
        var takeover = await fixture.Store.UpdateSpotAsync(
            initial with { OwnerId = OwnerB, OwnerNodeRid = RoutingId.From("node-2") },
            ZLinkLocationWriteIntent.Takeover);
        await using var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            handles,
            options);

        await host.ApplyAsync(
            new ZLinkLocationChanged(
                ZLinkLocationKind.Spot,
                new ZLinkLocationKey.Spot(new ZLinkSpotLocationKey(initial.SpotId)),
                ZLinkLocationChangeType.Upserted,
                takeover.Generation,
                DateTimeOffset.UtcNow),
            CancellationToken.None);

        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal("play", handle.Snapshot.RouterChannelId);
    }

    [Fact]
    public async Task Watch_Remove_Invalidates_The_Handle_Until_A_Newer_Row_Appears()
    {
        var fixture = await FixtureAsync();
        var initial = InMemoryLocationStoreTests.Spot(OwnerA, "spot-watch");
        var written = await fixture.Store.UpdateSpotAsync(initial, ZLinkLocationWriteIntent.NewClaim);
        var handles = new ZLinkSpotHandleRegistry();
        var resolver = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await resolver.ResolveSpotHandleAsync("play", initial.SpotId));
        await using var host = new ZLinkSpotHandleWatchHost(
            null,
            fixture.Resolvers,
            handles,
            new ZLinkLocationOptions { PollingInterval = TimeSpan.FromMinutes(1) });

        await host.ApplyAsync(
            new ZLinkLocationChanged(
                ZLinkLocationKind.Spot,
                new ZLinkLocationKey.Spot(new ZLinkSpotLocationKey(initial.SpotId)),
                ZLinkLocationChangeType.Removed,
                written.Generation,
                DateTimeOffset.UtcNow),
            CancellationToken.None);

        Assert.Throws<ZLinkFrameworkException>(() => _ = handle.Snapshot);

        handles.UpdateSpot(initial with
        {
            OwnerNodeRid = RoutingId.From("node-recovered"),
            SpotGeneration = written.Generation + 1
        });

        Assert.Equal(RoutingId.From("node-recovered"), handle.Snapshot.NodeRid);
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
                "spot-1",
                1),
            1,
            _ =>
            {
                refreshCalls++;
                return ValueTask.FromResult<(ZLinkSpotHandleSnapshot, ulong)?>(null);
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
    public void Spot_Handle_Does_Not_Apply_An_Older_Version()
    {
        var handle = new ZLinkResolvedSpotHandle(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-new"),
                "spot-new",
                2),
            2,
            _ => ValueTask.FromResult<(ZLinkSpotHandleSnapshot, ulong)?>(null));

        handle.Update(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-old"),
                "spot-old",
                1),
            1);
        handle.Invalidate(1);

        Assert.Equal(RoutingId.From("node-new"), handle.Snapshot.NodeRid);
        Assert.Equal("spot-new", handle.SpotId);

        handle.Invalidate(3);
        handle.Update(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-same"),
                "spot-same",
                3),
            3);
        handle.Update(
            new ZLinkSpotHandleSnapshot(
                "play",
                RoutingId.From("node-delayed"),
                "spot-delayed",
                2),
            2);
        Assert.Throws<ZLinkFrameworkException>(() => _ = handle.Snapshot);
    }

    [Fact]
    public async Task Actor_Address_Is_The_Entry_Spot_For_Entry_Actors_And_The_User_Spot_Otherwise()
    {
        var fixture = await FixtureAsync();
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());

        var entryActor = InMemoryLocationStoreTests.Actor(OwnerA);
        await fixture.Store.UpdateActorAsync(entryActor, ZLinkLocationWriteIntent.NewClaim);

        var entryAddress = await addresses.ResolveActorSpotHandleAsync("play", entryActor.ActorId);
        Assert.NotNull(entryAddress);
        Assert.Equal(entryActor.SpotId, entryAddress.SpotId);

        var userActor = InMemoryLocationStoreTests.Actor(OwnerA, "actor-2") with
        {
            ActorRef = new ActorRef(RoutingId.From("node-1"), "actor-2", 1),
            SpotKind = ZLinkSpotKind.User,
            SpotId = "spot-7"
        };
        await fixture.Store.UpdateActorAsync(userActor, ZLinkLocationWriteIntent.NewClaim);

        var userAddress = await addresses.ResolveActorSpotHandleAsync("play", userActor.ActorId);
        Assert.NotNull(userAddress);
        Assert.Equal("spot-7", userAddress.SpotId);
    }

    [Fact]
    public async Task Actor_Spot_Handle_Refreshes_By_Actor_Id_After_The_Actor_Moves()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA) with
        {
            SpotKind = ZLinkSpotKind.User,
            SpotId = "spot-old"
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            new ZLinkSpotHandleRegistry());
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", actor.ActorId));

        await fixture.Store.UpdateActorAsync(
            actor with
            {
                OwnerId = OwnerB,
                OwnerNodeRid = RoutingId.From("node-2"),
                SpotId = "spot-new",
                MembershipEpoch = actor.MembershipEpoch + 1
            },
            ZLinkLocationWriteIntent.Takeover);

        var attempts = new List<string>();
        var spotId = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
            handle,
            snapshot =>
            {
                attempts.Add(snapshot.SpotId);
                if (attempts.Count == 1)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RequestTargetNotFound,
                        "actor moved");
                return ValueTask.FromResult(snapshot.SpotId);
            },
            CancellationToken.None);

        Assert.Equal(["spot-old", "spot-new"], attempts);
        Assert.Equal("spot-new", spotId);
        Assert.Equal("spot-new", handle.SpotId);
    }

    [Fact]
    public async Task Location_Event_Updates_Existing_Actor_Spot_Handle_Snapshot()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA) with
        {
            SpotKind = ZLinkSpotKind.User,
            SpotId = "spot-old"
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var handles = new ZLinkSpotHandleRegistry();
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", actor.ActorId));

        handles.UpdateActor(actor with
        {
            OwnerNodeRid = RoutingId.From("node-2"),
            SpotId = "spot-new",
            MembershipEpoch = actor.MembershipEpoch + 1
        });

        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal("spot-new", handle.SpotId);
    }

    [Fact]
    public async Task Handle_Polling_Updates_Actor_Snapshot_When_Watch_Is_Unavailable()
    {
        var fixture = await FixtureAsync();
        var actor = InMemoryLocationStoreTests.Actor(OwnerA) with
        {
            SpotKind = ZLinkSpotKind.User,
            SpotId = "spot-old"
        };
        await fixture.Store.UpdateActorAsync(actor, ZLinkLocationWriteIntent.NewClaim);
        var locationOptions = new ZLinkLocationOptions
        {
            PollingInterval = TimeSpan.FromMilliseconds(10)
        };
        var handles = new ZLinkSpotHandleRegistry();
        var addresses = new ZLinkLocationAddressResolvers(
            fixture.Resolvers,
            handles);
        var handle = Assert.IsType<ZLinkResolvedSpotHandle>(
            await addresses.ResolveActorSpotHandleAsync("play", actor.ActorId));
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
                OwnerNodeRid = RoutingId.From("node-2"),
                SpotId = "spot-new",
                MembershipEpoch = actor.MembershipEpoch + 1
            },
            ZLinkLocationWriteIntent.Takeover);

        await WaitUntilAsync(
            () => handle.SpotId == "spot-new",
            TimeSpan.FromSeconds(2));
        Assert.Equal(RoutingId.From("node-2"), handle.Snapshot.NodeRid);
        Assert.Equal("play", handle.Snapshot.RouterChannelId);
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
    public async Task Older_Membership_Epoch_From_A_Lagging_Replica_Is_Never_A_Success()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMinutes(5));
        var options = new ZLinkLocationOptions { PollingInterval = TimeSpan.Zero };
        var tracker = new ZLinkOwnerLeaseTracker(store, options, time);

        // A replica store that first serves epoch 2, then lags back to
        // epoch 1, then catches up again.
        var replica = new ScriptedActorStore(
            InMemoryLocationStoreTests.Actor(OwnerA) with { MembershipEpoch = 2 },
            InMemoryLocationStoreTests.Actor(OwnerA) with { MembershipEpoch = 1 },
            InMemoryLocationStoreTests.Actor(OwnerA) with { MembershipEpoch = 2 });
        var resolvers = new ZLinkStoreLocationResolvers(
            store, store, replica, tracker, new ZLinkObservedLocationGenerations());

        var first = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(2UL, first!.MembershipEpoch);

        // The lagging read must not roll the observed view backwards.
        Assert.Null(await resolvers.ResolveActorRowAsync(ActorKey));

        // An equal epoch is accepted again.
        var third = await resolvers.ResolveActorRowAsync(ActorKey);
        Assert.Equal(2UL, third!.MembershipEpoch);
    }

    private static ZLinkFrameworkRegistration PlayRegistration()
    {
        var registration = new ZLinkFrameworkRegistration();
        registration.SpotNodes.Add(
            "play",
            new ZLinkSpotNodeRegistration
            {
                SpotNodeName = "play",
                SpotMeshChannelName = "play"
            });
        return registration;
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

        public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
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

        public ValueTask<ZLinkLocationWriteStatus> RemoveActorAsync(
            ZLinkActorLocationKey key,
            ZLinkLocationOwnerToken owner,
            CancellationToken cancellationToken = default) =>
            inner.RemoveActorAsync(key, owner, cancellationToken);
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
            store, store, store, tracker, observed);
        return new ResolverFixture(store, resolvers, time);
    }

    private sealed record ResolverFixture(
        ZLinkInMemoryLocationStore Store,
        ZLinkStoreLocationResolvers Resolvers,
        ManualTimeProvider Time);
}
