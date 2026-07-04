using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>
/// Behavioral parity is a contract requirement (draft 13절): a scenario that
/// passes on the in-memory store must produce the same observable write
/// results on the official Redis extension. This runs one operation script
/// against both stores and compares every status and store-issued
/// generation. Store-clock timestamps are excluded; the two stores read
/// different clocks by design.
/// </summary>
[Collection(RedisTestCollection.Name)]
public sealed class RedisInMemoryParityTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";

    private readonly RedisTestFixture _fixture;

    public RedisInMemoryParityTests(RedisTestFixture fixture)
    {
        _fixture = fixture;
    }

    [SkippableFact]
    public async Task Same_Operation_Sequence_Yields_Identical_Statuses_And_Generations()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var redisStore = _fixture.CreateStore();
        var inMemoryStore = new ZLinkInMemoryLocationStore();

        var redisTrace = await RunScenarioAsync(
            redisStore, redisStore, redisStore, redisStore, redisStore, redisStore);
        var inMemoryTrace = await RunScenarioAsync(
            inMemoryStore, inMemoryStore, inMemoryStore, inMemoryStore, inMemoryStore, inMemoryStore);

        Assert.Equal(inMemoryTrace, redisTrace);
    }

    private static async Task<IReadOnlyList<string>> RunScenarioAsync(
        IZLinkPeerLocationStore peers,
        IZLinkSpotLocationStore spots,
        IZLinkActorLocationStore actors,
        IZLinkRouteLocationStore routes,
        IZLinkOwnerLeaseStore leases,
        IZLinkLocationStore store)
    {
        var trace = new List<string>();
        void Record(string step, ZLinkLocationWriteResult result) =>
            trace.Add($"{step}={result.Status}:{result.Generation}");
        void RecordLease(string step, ZLinkOwnerLeaseRenewal renewal) =>
            trace.Add($"{step}=renewed:{renewal.LeaseExpiresAt > renewal.StoreNow}");
        void RecordBool(string step, bool removed) =>
            trace.Add($"{step}=removed:{removed}");

        var leaseTtl = TimeSpan.FromSeconds(30);
        RecordLease("lease-a", await leases.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), leaseTtl));
        RecordLease("lease-b", await leases.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), leaseTtl));

        // Actor lifecycle: claim, conflict, renew guard, takeover fencing,
        // owner-guarded remove, re-claim after removal.
        var claim = await actors.UpdateActorAsync(TestRows.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim);
        Record("actor-claim", claim);
        Record("actor-claim-conflict",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerB, 0), ZLinkLocationWriteIntent.NewClaim));
        Record("actor-renew",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA, claim.Generation), ZLinkLocationWriteIntent.Renew));
        Record("actor-renew-wrong-generation",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA, claim.Generation + 9), ZLinkLocationWriteIntent.Renew));
        var takeover = await actors.UpdateActorAsync(TestRows.Actor(OwnerB, 0), ZLinkLocationWriteIntent.Takeover);
        Record("actor-takeover", takeover);
        Record("actor-old-owner-renew",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA, claim.Generation), ZLinkLocationWriteIntent.Renew));
        Record("actor-old-owner-remove", await actors.RemoveActorAsync(
            new ZLinkActorLocationKey("actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claim.Generation)));
        Record("actor-remove", await actors.RemoveActorAsync(
            new ZLinkActorLocationKey("actor-1"),
            new ZLinkLocationOwnerToken(OwnerB, takeover.Generation)));
        Record("actor-reclaim",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA, 0), ZLinkLocationWriteIntent.NewClaim));

        // Spot rows plus bulk removal by owner.
        Record("spot-claim-1",
            await spots.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim));
        Record("spot-claim-2",
            await spots.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-2"), ZLinkLocationWriteIntent.NewClaim));
        Record("spot-remove-wrong-owner", await spots.RemoveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-1")),
            new ZLinkLocationOwnerToken(OwnerB, 1)));
        trace.Add($"remove-all-by-owner={await store.RemoveAllByOwnerAsync(OwnerA)}");

        // Route claim and generation continuity for a second claim cycle.
        var routeClaim = await routes.UpdateRouteAsync(TestRows.Route(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Record("route-claim", routeClaim);
        Record("route-remove", await routes.RemoveRouteAsync(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ActorSession, "route-1"),
            new ZLinkLocationOwnerToken(OwnerA, routeClaim.Generation)));
        Record("route-reclaim",
            await routes.UpdateRouteAsync(TestRows.Route(OwnerB), ZLinkLocationWriteIntent.NewClaim));

        // A removed owner lease makes that owner's remaining rows claimable.
        var peerClaim = await peers.UpdatePeerAsync(TestRows.Peer(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Record("peer-claim", peerClaim);
        RecordBool("lease-a-remove", await leases.RemoveOwnerLeaseAsync(OwnerA));
        Record("peer-claim-after-lease-removed",
            await peers.UpdatePeerAsync(TestRows.Peer(OwnerB), ZLinkLocationWriteIntent.NewClaim));

        return trace;
    }
}
