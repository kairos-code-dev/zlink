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
    public async Task RoutingIdSlots_DoNotInferIdentityModeFromPeerRows()
    {
        var store = new ZLinkInMemoryLocationStore();
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("fixed-node"), LeaseTtl);
        await store.UpdateMeshNodeAsync(
            MeshNode(OwnerA, endpoint: "tcp://127.0.0.1:5001", nodeRid: "fixed-node"),
            ZLinkLocationWriteIntent.NewClaim);

        var result = await store.AcquireRoutingIdSlotAsync(new ZLinkRoutingIdSlotAcquireRequest(
            "play",
            [new ZLinkRoutingIdSlotAllocationMember("play", "play")],
            10,
            OwnerB,
            LeaseTtl));

        Assert.IsType<ZLinkRoutingIdSlotAcquired>(result);
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

        var first = await store.UpdateActorAsync(Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);
        Assert.Equal(1UL, first.Generation);

        var conflict = await store.UpdateActorAsync(Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, conflict.Status);

        var removed = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, first.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);

        // Generation counters survive removal so a re-claim can never reuse
        // an old fencing token.
        var reclaimed = await store.UpdateActorAsync(Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(2UL, reclaimed.Generation);
    }

    [Fact]
    public async Task NewClaim_Succeeds_Over_Row_Whose_Owner_Lease_Expired()
    {
        var time = new ManualTimeProvider();
        var store = new ZLinkInMemoryLocationStore(time);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMinutes(5));

        var first = await store.UpdateActorAsync(Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);

        // Owner A stops heartbeating; its lease expires and its rows become
        // claimable without any row write.
        time.Advance(LeaseTtl + TimeSpan.FromSeconds(1));

        var reclaimed = await store.UpdateActorAsync(Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, reclaimed.Status);
        Assert.Equal(2UL, reclaimed.Generation);
    }

    [Fact]
    public async Task Renew_Requires_Current_Owner_And_Keeps_Generation()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var claimed = await store.UpdateActorAsync(Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        var renewed = await store.UpdateActorAsync(
            Actor(OwnerA), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(claimed.Generation, renewed.Generation);

        var wrongOwner = await store.UpdateActorAsync(
            Actor(OwnerB), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, wrongOwner.Status);
    }

    [Fact]
    public async Task Takeover_Replaces_Live_Row_And_Old_Owner_Writes_Become_Stale()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var claimed = await store.UpdateActorAsync(Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        var takeover = await store.UpdateActorAsync(Actor(OwnerB), ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.True(takeover.Generation > claimed.Generation);

        // The replaced owner learns it lost ownership from its next write.
        var staleRenew = await store.UpdateActorAsync(
            Actor(OwnerA), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRenew.Status);

        var staleRemove = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRemove);
    }

    [Fact]
    public async Task RemoveByOwner_Bulk_Removes_Only_That_Owners_Rows()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        IZLinkActorLocationStore actors = store;
        await actors.UpdateActorAsync(Actor(OwnerA, "actor-1"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(Actor(OwnerA, "actor-2"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(Actor(OwnerB, "actor-3"), ZLinkLocationWriteIntent.NewClaim);

        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;
        Assert.Equal(
            0,
            await store.RemoveAllByOwnerAsync(
                ownerAToken with
                {
                    LeaseGeneration = ownerAToken.LeaseGeneration + 1
                }));
        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(2, removed);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-3")));
    }

    [Fact]
    public async Task RemoveAllByOwner_Removes_All_Kinds_For_The_Owner()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA, OwnerB);
        var descriptorA = MeshNode(OwnerA, endpoint: "tcp://127.0.0.1:5001", nodeRid: "node-a");
        var descriptorB = MeshNode(
            OwnerB,
            endpoint: "tcp://127.0.0.1:5002",
            nodeRid: "node-b",
            leaseGeneration: 2);

        await store.UpdateMeshNodeAsync(descriptorA, ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(descriptorB, ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(Spot(OwnerA, "spot-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(Spot(OwnerB, "spot-b"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(Actor(OwnerA, "actor-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(Actor(OwnerB, "actor-b"), ZLinkLocationWriteIntent.NewClaim);

        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;
        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(3, removed);
        Assert.DoesNotContain(await store.ListMeshNodesAsync("play"), row => row.OwnerId == OwnerA);
        Assert.Contains(await store.ListMeshNodesAsync("play"), row => row.OwnerId == OwnerB);
        Assert.Null(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("play", RoutingId.From("spot-a"))));
        Assert.NotNull(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("play", RoutingId.From("spot-b"))));
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-a")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-b")));
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
    public async Task Mesh_List_Returns_Only_The_Requested_Mesh_Snapshot()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        await store.UpdateMeshNodeAsync(
            MeshNode(OwnerA, nodeRid: "node-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(
            MeshNode(OwnerA, nodeRid: "node-b", meshName: "world"),
            ZLinkLocationWriteIntent.NewClaim);

        var play = await store.ListMeshNodesAsync("play");

        Assert.Equal(RoutingId.From("node-a"), Assert.Single(play).Rid);
    }

    [Fact]
    public async Task Change_Stamp_Increments_On_Writes_And_Is_Stable_On_Reads()
    {
        var (store, _) = await CreateStoreWithLiveOwnersAsync(OwnerA);
        var scope = new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play");

        var before = await store.GetChangeStampAsync(scope);
        await store.UpdateMeshNodeAsync(MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var afterWrite = await store.GetChangeStampAsync(scope);
        await store.ListMeshNodesAsync("play");
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

    internal static ZLinkActorLocation Actor(string ownerId, string actorId = "actor-1") => new(
        "play",
        actorId,
        "player",
        new ActorRef(RoutingId.From("node-1"), actorId, 1),
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 0,
        SpotId: default,
        SpotGeneration: 0,
        SpotKind: ZLinkSpotKind.Entry,
        MembershipEpoch: 0,
        OwnerId: ownerId,
        UpdatedAt: default);

    internal static ZLinkSpotLocation Spot(string ownerId, string spotId) => new(
        "play",
        RoutingId.From(spotId),
        SpotGeneration: 0,
        OwnerNodeRid: RoutingId.From("node-1"),
        OwnerNodeGeneration: 0,
        SpotKind: ZLinkSpotKind.User,
        SpotType: "game",
        OwnerId: ownerId,
        UpdatedAt: default);

    internal static ZLinkMeshNodeDescriptor MeshNode(
        string ownerId,
        string endpoint = "tcp://127.0.0.1:5001",
        string nodeRid = "node-1",
        string meshName = "play",
        long leaseGeneration = 1) => new(
        meshName,
        RoutingId.From(nodeRid),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        endpoint,
        new Dictionary<string, int>(StringComparer.Ordinal) { [meshName] = 100 },
        SecurityIdentity: string.Empty,
        OwnerId: ownerId,
        LeaseGeneration: leaseGeneration,
        UpdatedAt: default)
    {
        State = ZLinkFrameworkRuntimeState.Serving
    };
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
