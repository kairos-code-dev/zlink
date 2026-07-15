using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class InMemoryLocationStoreTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

    [Fact]
    public async Task RoutingIdSlots_AssignLowestAvailableAndFenceStaleRelease()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone-") };

        var first = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 3, OwnerA, LeaseTtl)));
        var second = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 3, OwnerB, LeaseTtl)));
        Assert.Equal(1, first.Allocation.Slot);
        Assert.Equal(2, second.Allocation.Slot);

        Assert.Equal(
            ZLinkRoutingIdSlotReleaseResult.Released,
            await store.ReleaseRoutingIdSlotAsync("zone", 1, first.Allocation.Owner));
        var replacement = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 3, "owner-c", LeaseTtl)));
        Assert.Equal(1, replacement.Allocation.Slot);
        Assert.True(replacement.Allocation.Owner.Generation > first.Allocation.Owner.Generation);
        Assert.Equal(
            ZLinkRoutingIdSlotReleaseResult.IgnoredStale,
            await store.ReleaseRoutingIdSlotAsync("zone", 1, first.Allocation.Owner));
    }

    [Fact]
    public async Task RoutingIdSlots_AreConcurrentIdempotentAndGroupScoped()
    {
        var store = new ZLinkInMemoryLocationStore();
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone") };
        var acquisitions = await Task.WhenAll(Enumerable.Range(1, 100).Select(async owner =>
            Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
                new ZLinkRoutingIdSlotAcquireRequest(
                    "zone",
                    members,
                    100,
                    $"owner-{owner}",
                    LeaseTtl)))));

        Assert.Equal(Enumerable.Range(1, 100), acquisitions.Select(static result => result.Allocation.Slot).Order());
        Assert.IsType<ZLinkRoutingIdSlotGroupExhausted>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 100, "overflow", LeaseTtl)));

        var retried = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 100, "owner-1", LeaseTtl)));
        Assert.Equal(acquisitions.Single(result => result.Allocation.Owner.OwnerId == "owner-1").Allocation.Owner, retried.Allocation.Owner);

        var otherGroup = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("other", members, 100, "other-owner", LeaseTtl)));
        Assert.Equal(1, otherGroup.Allocation.Slot);
    }

    [Fact]
    public async Task RoutingIdSlots_RejectChangedGroupConfigurationAndRecycleLogicalExpiry()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone") };
        var first = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerA, LeaseTtl)));

        Assert.IsType<ZLinkRoutingIdSlotGroupConfigurationMismatch>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest(
                "zone",
                [new ZLinkRoutingIdSlotAllocationMember("zone", "changed")],
                1,
                OwnerB,
                LeaseTtl)));

        time.Advance(LeaseTtl + TimeSpan.FromMilliseconds(1));
        var recycled = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerB, LeaseTtl)));
        Assert.Equal(first.Allocation.Slot, recycled.Allocation.Slot);
        Assert.True(recycled.Allocation.Owner.Generation > first.Allocation.Owner.Generation);
    }

    [Fact]
    public async Task RoutingIdSlots_RejectActiveFixedPeerWhenGroupIsFirstCreated()
    {
        var store = new ZLinkInMemoryLocationStore();
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("fixed-node"), LeaseTtl);
        await store.UpdatePeerAsync(
            Peer(OwnerA, endpoint: "tcp://127.0.0.1:5001", nodeRid: "fixed-node"),
            ZLinkLocationWriteIntent.NewClaim);

        var result = await store.AcquireRoutingIdSlotAsync(new ZLinkRoutingIdSlotAcquireRequest(
            "play",
            [new ZLinkRoutingIdSlotAllocationMember("play", "play")],
            10,
            OwnerB,
            LeaseTtl));

        Assert.IsType<ZLinkRoutingIdSlotIdentityModeConflict>(result);
    }

    [Fact]
    public async Task RoutingIdSlots_FollowTheSharedOwnerLeaseRenewal()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone") };
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-a"), LeaseTtl);
        await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerA, LeaseTtl));

        time.Advance(TimeSpan.FromSeconds(10));
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-a"), LeaseTtl);
        time.Advance(TimeSpan.FromSeconds(10));

        Assert.IsType<ZLinkRoutingIdSlotGroupExhausted>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerB, LeaseTtl)));
        var snapshot = await store.ListRoutingIdSlotsAsync("zone");
        Assert.True(Assert.Single(snapshot.Allocations).LeaseExpiresAt > snapshot.StoreNow);
    }

    [Fact]
    public async Task NewClaim_Issues_Monotonic_Generations_And_Rejects_Live_Row_Claims()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);

        var first = await store.UpdateActorAsync(Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);
        Assert.Equal(1, first.Generation);

        var conflict = await store.UpdateActorAsync(Actor(OwnerB, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, conflict.Status);

        var removed = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, first.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed.Status);

        // Generation counters survive removal so a re-claim can never reuse
        // an old fencing token.
        var reclaimed = await store.UpdateActorAsync(Actor(OwnerB, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(2, reclaimed.Generation);
    }

    [Fact]
    public async Task NewClaim_Succeeds_Over_Row_Whose_Owner_Lease_Expired()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));

        var first = await store.UpdateActorAsync(Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);

        // Owner A stops heartbeating; its lease expires and its rows become
        // claimable without any row write.
        time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var reclaimed = await store.UpdateActorAsync(Actor(OwnerB, 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, reclaimed.Status);
        Assert.Equal(2, reclaimed.Generation);
    }

    [Fact]
    public async Task Renew_Requires_Current_Owner_Token_And_Keeps_Generation()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var claimed = await store.UpdateActorAsync(Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        var renewed = await store.UpdateActorAsync(
            Actor(OwnerA, claimed.Generation), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(claimed.Generation, renewed.Generation);

        var wrongGeneration = await store.UpdateActorAsync(
            Actor(OwnerA, claimed.Generation + 7), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, wrongGeneration.Status);

        var wrongOwner = await store.UpdateActorAsync(
            Actor(OwnerB, claimed.Generation), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, wrongOwner.Status);
    }

    [Fact]
    public async Task Takeover_Replaces_Live_Row_And_Old_Owner_Writes_Become_Stale()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var claimed = await store.UpdateActorAsync(Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);

        var takeover = await store.UpdateActorAsync(Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.True(takeover.Generation > claimed.Generation);

        // The replaced owner learns it lost ownership from its next write.
        var staleRenew = await store.UpdateActorAsync(
            Actor(OwnerA, claimed.Generation), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRenew.Status);

        var staleRemove = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRemove.Status);
    }

    [Fact]
    public async Task RemoveByOwner_Bulk_Removes_Only_That_Owners_Rows()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        IZLinkActorLocationStore actors = store;
        await actors.UpdateActorAsync(Actor(OwnerA, 0, "actor-1"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(Actor(OwnerA, 0, "actor-2"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(Actor(OwnerB, 0, "actor-3"), ZLinkLocationWriteIntent.NewClaim);

        var removed = await store.RemoveAllByOwnerAsync(OwnerA);

        Assert.Equal(2, removed);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("actor-1")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("actor-3")));
    }

    [Fact]
    public async Task RemoveAllByOwner_Removes_All_Four_Kinds_For_The_Owner()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var peerA = Peer(OwnerA, endpoint: "tcp://127.0.0.1:5001", nodeRid: "node-a");
        var peerB = Peer(OwnerB, endpoint: "tcp://127.0.0.1:5002", nodeRid: "node-b");

        await store.UpdatePeerAsync(peerA, ZLinkLocationWriteIntent.NewClaim);
        await store.UpdatePeerAsync(peerB, ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(Spot(OwnerA, "spot-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(Spot(OwnerB, "spot-b"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(Actor(OwnerA, 0, "actor-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(Actor(OwnerB, 0, "actor-b"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateRouteAsync(Route(OwnerA, "route-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateRouteAsync(Route(OwnerB, "route-b"), ZLinkLocationWriteIntent.NewClaim);

        var removed = await store.RemoveAllByOwnerAsync(OwnerA);

        Assert.Equal(4, removed);
        Assert.DoesNotContain(await store.ListPeersAsync(new ZLinkPeerLocationFilter()), row => row.OwnerId == OwnerA);
        Assert.Empty((await store.ListSpotsAsync(new ZLinkSpotLocationFilter())).Items.Where(row => row.OwnerId == OwnerA));
        Assert.Empty((await store.ListActorsAsync(new ZLinkActorLocationFilter())).Items.Where(row => row.OwnerId == OwnerA));
        Assert.Empty((await store.ListRoutesAsync(new ZLinkRouteLocationFilter())).Items.Where(row => row.OwnerId == OwnerA));
        Assert.Contains(await store.ListPeersAsync(new ZLinkPeerLocationFilter()), row => row.OwnerId == OwnerB);
        Assert.NotNull(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("play", RoutingId.From("spot-b"))));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("actor-b")));
        Assert.NotNull(await store.ResolveRouteAsync(new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "route-b")));
    }

    [Fact]
    public async Task RenewOwnerLease_Returns_Renewal_Using_Store_Clock()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        var ttl = TimeSpan.FromSeconds(42);
        var expectedStoreNow = time.GetUtcNow();

        var renewal = await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), ttl);

        Assert.Equal(expectedStoreNow, renewal.StoreNow);
        Assert.Equal(expectedStoreNow + ttl, renewal.LeaseExpiresAt);
        time.AdvanceWallClockOnly(TimeSpan.FromMinutes(10));
        var snapshot = await store.ListOwnerLeasesAsync();
        var lease = Assert.Single(snapshot.Leases);
        Assert.Equal(time.GetUtcNow(), snapshot.StoreNow);
        Assert.Equal(renewal.LeaseExpiresAt, lease.LeaseExpiresAt);
    }

    [Fact]
    public async Task Paged_List_Traverses_All_Rows_With_Continuation_Tokens()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        for (var i = 0; i < 5; i++)
        {
            await store.UpdateSpotAsync(Spot(OwnerA, $"spot-{i}"), ZLinkLocationWriteIntent.NewClaim);
        }

        var collected = new List<ZLinkSpotLocation>();
        string? token = null;
        do
        {
            var page = await store.ListSpotsAsync(
                new ZLinkSpotLocationFilter(MeshName: "play"),
                new ZLinkPageRequest(PageSize: 2, ContinuationToken: token));
            collected.AddRange(page.Items);
            token = page.ContinuationToken;
        }
        while (token is not null);

        Assert.Equal(5, collected.Count);
        Assert.Equal(5, collected.Select(spot => spot.SpotRid).Distinct().Count());
    }

    [Fact]
    public async Task List_Filters_Match_Row_Fields()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        IZLinkActorLocationStore actors = store;
        await actors.UpdateActorAsync(Actor(OwnerA, 0, "actor-1"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(
            Actor(OwnerA, 0, "actor-2") with { ActorType = "npc" },
            ZLinkLocationWriteIntent.NewClaim);

        var players = await actors.ListActorsAsync(new ZLinkActorLocationFilter(ActorType: "player"));
        var everyone = await actors.ListActorsAsync(new ZLinkActorLocationFilter());

        Assert.Single(players.Items);
        Assert.Equal(2, everyone.Items.Count);
    }

    [Fact]
    public async Task Actor_List_Filters_By_Node_Rid_Spot_Rid_And_Location_Kind()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        IZLinkActorLocationStore actors = store;
        await actors.UpdateActorAsync(Actor(OwnerA, 0, "actor-1"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(
            Actor(OwnerA, 0, "actor-2") with
            {
                NodeRid = RoutingId.From("node-2"),
                LocationKind = ZLinkSpotKind.User,
                SpotRid = RoutingId.From("spot-9")
            },
            ZLinkLocationWriteIntent.NewClaim);

        var byNode = await actors.ListActorsAsync(
            new ZLinkActorLocationFilter(NodeRid: RoutingId.From("node-2")));
        Assert.Equal("actor-2", Assert.Single(byNode.Items).ActorId);

        var bySpot = await actors.ListActorsAsync(
            new ZLinkActorLocationFilter(SpotRid: RoutingId.From("spot-9")));
        Assert.Equal("actor-2", Assert.Single(bySpot.Items).ActorId);

        var entryOnly = await actors.ListActorsAsync(
            new ZLinkActorLocationFilter(LocationKind: ZLinkSpotKind.Entry));
        Assert.Equal("actor-1", Assert.Single(entryOnly.Items).ActorId);
    }

    [Fact]
    public async Task Spot_List_Filters_By_Spot_Type_Node_Rid_And_Spot_Kind()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        IZLinkSpotLocationStore spots = store;
        await spots.UpdateSpotAsync(Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim);
        await spots.UpdateSpotAsync(
            Spot(OwnerA, "spot-2") with
            {
                SpotType = "lobby",
                NodeRid = RoutingId.From("node-2"),
                SpotKind = ZLinkSpotKind.Entry
            },
            ZLinkLocationWriteIntent.NewClaim);

        var byType = await spots.ListSpotsAsync(new ZLinkSpotLocationFilter(SpotType: "lobby"));
        Assert.Equal(RoutingId.From("spot-2"), Assert.Single(byType.Items).SpotRid);

        var byNode = await spots.ListSpotsAsync(
            new ZLinkSpotLocationFilter(NodeRid: RoutingId.From("node-2")));
        Assert.Equal(RoutingId.From("spot-2"), Assert.Single(byNode.Items).SpotRid);

        var userOnly = await spots.ListSpotsAsync(
            new ZLinkSpotLocationFilter(SpotKind: ZLinkSpotKind.User));
        Assert.Equal(RoutingId.From("spot-1"), Assert.Single(userOnly.Items).SpotRid);
    }

    [Fact]
    public async Task Route_List_Filters_By_Kind_Owner_Node_Rid_And_Owner_Id()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        IZLinkRouteLocationStore routes = store;
        await routes.UpdateRouteAsync(Route(OwnerA, "session-1"), ZLinkLocationWriteIntent.NewClaim);
        await routes.UpdateRouteAsync(
            Route(OwnerB, "spot-name-1") with
            {
                RouteKind = ZLinkRouteKind.SpotName,
                OwnerNodeRid = RoutingId.From("node-2")
            },
            ZLinkLocationWriteIntent.NewClaim);

        var byKind = await routes.ListRoutesAsync(
            new ZLinkRouteLocationFilter(RouteKind: ZLinkRouteKind.SpotName));
        Assert.Equal("spot-name-1", Assert.Single(byKind.Items).RouteKey);

        var byOwnerNode = await routes.ListRoutesAsync(
            new ZLinkRouteLocationFilter(OwnerNodeRid: RoutingId.From("node-2")));
        Assert.Equal("spot-name-1", Assert.Single(byOwnerNode.Items).RouteKey);

        var byOwnerId = await routes.ListRoutesAsync(new ZLinkRouteLocationFilter(OwnerId: OwnerA));
        Assert.Equal("session-1", Assert.Single(byOwnerId.Items).RouteKey);
    }

    [Fact]
    public async Task Actor_And_Route_Paged_Lists_Traverse_All_Rows_With_Continuation_Tokens()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        for (var i = 0; i < 5; i++)
        {
            await store.UpdateActorAsync(Actor(OwnerA, 0, $"actor-{i}"), ZLinkLocationWriteIntent.NewClaim);
            await store.UpdateRouteAsync(Route(OwnerA, $"route-{i}"), ZLinkLocationWriteIntent.NewClaim);
        }

        var actors = new List<ZLinkActorLocation>();
        string? token = null;
        do
        {
            var page = await store.ListActorsAsync(
                new ZLinkActorLocationFilter(ActorType: "player"),
                new ZLinkPageRequest(PageSize: 2, ContinuationToken: token));
            actors.AddRange(page.Items);
            token = page.ContinuationToken;
        }
        while (token is not null);

        Assert.Equal(5, actors.Select(static actor => actor.ActorId).Distinct().Count());

        var routes = new List<ZLinkRouteLocation>();
        token = null;
        do
        {
            var page = await store.ListRoutesAsync(
                new ZLinkRouteLocationFilter(RouteKind: ZLinkRouteKind.ActorSession),
                new ZLinkPageRequest(PageSize: 2, ContinuationToken: token));
            routes.AddRange(page.Items);
            token = page.ContinuationToken;
        }
        while (token is not null);

        Assert.Equal(5, routes.Select(static route => route.RouteKey).Distinct().Count());
    }

    [Fact]
    public async Task Change_Stamp_Increments_On_Writes_And_Is_Stable_On_Reads()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        var scope = new ZLinkLocationChangeStampScope(ZLinkLocationKind.Peer, "play");

        var before = await store.GetChangeStampAsync(scope);
        await store.UpdatePeerAsync(Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var afterWrite = await store.GetChangeStampAsync(scope);
        await store.ListPeersAsync(new ZLinkPeerLocationFilter(MeshName: "play"));
        var afterRead = await store.GetChangeStampAsync(scope);

        Assert.True(afterWrite > before);
        Assert.Equal(afterWrite, afterRead);
    }

    private static async Task<(ZLinkInMemoryLocationStore Store, ManualTimeProvider Time)>
        CreateStoreWithLiveOwnersAsync(params string[] owners)
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        foreach (var owner in owners)
        {
            await store.RenewOwnerLeaseAsync(owner, RoutingId.From("node-1"), LeaseTtl);
        }

        return (store, time);
    }

    internal static ZLinkActorLocation Actor(string ownerId, long generation, string actorId = "actor-1") => new(
        actorId,
        "player",
        new ActorRef(RoutingId.From("node-1"), actorId, 1),
        RoutingId.From("node-1"),
        ZLinkSpotKind.Entry,
        "play",
        null,
        ownerId,
        generation,
        default);

    internal static ZLinkSpotLocation Spot(string ownerId, string spotRid) => new(
        "play",
        RoutingId.From(spotRid),
        "game",
        RoutingId.From("node-1"),
        ZLinkSpotKind.User,
        null,
        ownerId,
        0,
        default);

    internal static ZLinkPeerLocation Peer(
        string ownerId,
        string endpoint = "tcp://127.0.0.1:5001",
        string nodeRid = "node-1") => new(
        ZLinkLocationAutoConnectType.RouteMesh,
        "play",
        RoutingId.From(nodeRid),
        ZLinkLocationRole.Router,
        endpoint,
        100,
        false,
        0,
        null,
        null,
        ownerId,
        0,
        default);

    internal static ZLinkRouteLocation Route(
        string ownerId,
        string routeKey = "route-1",
        long generation = 0) => new(
        ZLinkRouteKind.ActorSession,
        routeKey,
        RoutingId.From("node-1"),
        ownerId,
        generation,
        new byte[] { 1, 2, 3, 4 },
        default);
}

/// <summary>
/// Deterministic clock for lease and cache TTL tests. Wall time and the
/// monotonic timestamp advance together.
/// </summary>
internal sealed class ManualTimeProvider : TimeProvider
{
    private DateTimeOffset _utcNow = new(2026, 7, 2, 0, 0, 0, TimeSpan.Zero);
    private long _timestamp;

    public override DateTimeOffset GetUtcNow() => _utcNow;

    public override long GetTimestamp() => _timestamp;

    public override long TimestampFrequency => TimeSpan.TicksPerSecond;

    public void Advance(TimeSpan delta)
    {
        _utcNow += delta;
        _timestamp += delta.Ticks;
    }

    /// <summary>A wall-clock jump without monotonic progress, for asserting
    /// that lease expiry never compares application wall clocks (draft 6.6).</summary>
    public void AdvanceWallClockOnly(TimeSpan delta)
    {
        _utcNow += delta;
    }

    public void AdvanceMonotonicOnly(TimeSpan delta)
    {
        _timestamp += delta.Ticks;
    }
}
