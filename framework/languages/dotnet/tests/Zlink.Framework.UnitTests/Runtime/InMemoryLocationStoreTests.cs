using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.UnitTests;

public sealed class InMemoryLocationStoreTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(15);

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
            new ZLinkActorLocationKey("player", "actor-1"),
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
            new ZLinkActorLocationKey("player", "actor-1"),
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

        var removed = await actors.RemoveByOwnerAsync(OwnerA);

        Assert.Equal(2, removed);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "actor-1")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "actor-3")));
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
        "player",
        actorId,
        "actor-ref",
        RoutingId.From("node-1"),
        generation,
        ZLinkSpotKind.Entry,
        null,
        ZLinkSpotKind.Entry,
        ownerId,
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
        0,
        null,
        null,
        ownerId,
        0,
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
}
