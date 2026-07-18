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
            redisStore, redisStore, redisStore, redisStore, redisStore);
        var inMemoryTrace = await RunScenarioAsync(
            inMemoryStore, inMemoryStore, inMemoryStore, inMemoryStore, inMemoryStore);

        Assert.Equal(inMemoryTrace, redisTrace);
    }

    private static async Task<IReadOnlyList<string>> RunScenarioAsync(
        IZLinkMeshNodeLocationStore meshNodes,
        IZLinkSpotLocationStore spots,
        IZLinkActorLocationStore actors,
        IZLinkOwnerLeaseStore leases,
        IZLinkLocationStore store)
    {
        var trace = new List<string>();
        void Record(string step, ZLinkLocationWriteResult result) =>
            trace.Add($"{step}={result.Status}:{result.Generation}");
        void RecordStatus(string step, ZLinkLocationWriteStatus status) =>
            trace.Add($"{step}={status}");
        void RecordLease(string step, ZLinkOwnerLeaseRenewal renewal) =>
            trace.Add($"{step}=renewed:{renewal.LeaseExpiresAt > renewal.StoreNow}");
        void RecordBool(string step, bool removed) =>
            trace.Add($"{step}=removed:{removed}");

        var leaseTtl = TimeSpan.FromSeconds(30);
        RecordLease("lease-a", await leases.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), leaseTtl));
        RecordLease("lease-b", await leases.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), leaseTtl));

        // Actor lifecycle: claim, conflict, owner-guarded renew, takeover
        // fencing, owner-guarded remove, re-claim after removal.
        var claim = await actors.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Record("actor-claim", claim);
        Record("actor-claim-conflict",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim));
        Record("actor-renew",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.Renew));
        var takeover = await actors.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.Takeover);
        Record("actor-takeover", takeover);
        Record("actor-old-owner-renew",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.Renew));
        RecordStatus("actor-old-owner-remove", await actors.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claim.Generation)));
        RecordStatus("actor-remove", await actors.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerB, takeover.Generation)));
        Record("actor-reclaim",
            await actors.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim));

        // Spot rows plus bulk removal by owner.
        Record("spot-claim-1",
            await spots.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim));
        Record("spot-claim-2",
            await spots.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-2"), ZLinkLocationWriteIntent.NewClaim));
        RecordStatus("spot-remove-wrong-owner", await spots.RemoveSpotAsync(
            new ZLinkSpotLocationKey("play", RoutingId.From("spot-1")),
            new ZLinkLocationOwnerToken(OwnerB, 1)));
        trace.Add($"remove-all-by-owner={await store.RemoveAllByOwnerAsync(OwnerA)}");

        // A removed owner lease makes that owner's remaining rows claimable.
        var descriptorClaim = await meshNodes.UpdateMeshNodeAsync(
            TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Record("mesh-node-claim", descriptorClaim);
        RecordBool("lease-a-remove", await leases.RemoveOwnerLeaseAsync(OwnerA));
        Record("mesh-node-claim-after-lease-removed",
            await meshNodes.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerB), ZLinkLocationWriteIntent.NewClaim));

        return trace;
    }
}
