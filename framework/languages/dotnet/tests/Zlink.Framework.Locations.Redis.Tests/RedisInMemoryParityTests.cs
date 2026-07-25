using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>
/// Runs the same authority and owner-lifecycle operations against the
/// in-memory provider and the official Redis extension. Store versions and
/// timestamps are provider-owned, so parity compares result kinds, fencing
/// generations and final visibility instead.
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

        var redisTrace = await RunScenarioAsync(redisStore);
        var inMemoryTrace = await RunScenarioAsync(inMemoryStore);

        Assert.Equal(inMemoryTrace, redisTrace);
    }

    private static async Task<IReadOnlyList<string>> RunScenarioAsync(
        IZLinkLocationStore store)
    {
        var trace = new List<string>();

        var leaseTtl = TimeSpan.FromSeconds(30);
        var ownerA = await store.ClaimOwnerLeaseAsync(OwnerA, leaseTtl);
        var ownerB = await store.ClaimOwnerLeaseAsync(OwnerB, leaseTtl);
        RecordLease(trace, "lease-a", ownerA);
        RecordLease(trace, "lease-b", ownerB);
        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(ownerA).Token;
        var ownerBToken = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(ownerB).Token;
        await store.RenewOwnerLeaseAsync(ownerAToken, leaseTtl);
        await store.RenewOwnerLeaseAsync(ownerBToken, leaseTtl);

        await PublishDescriptorAsync(store, ownerAToken, "node-a");
        await PublishDescriptorAsync(store, ownerBToken, "node-b");

        var firstReserve = await ReserveActorAsync(
            store, ownerAToken, "node-a", "actor-1");
        RecordReserve(trace, "actor-reserve", firstReserve);
        var first = Assert.IsType<ZLinkObjectReserveResult.Reserved>(firstReserve);

        RecordReserve(
            trace,
            "actor-reserve-conflict",
            await ReserveActorAsync(store, ownerBToken, "node-b", "actor-1"));

        var committed = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(first.Reservation, new byte[] { 0x22 }));
        RecordSnapshot(trace, "actor-commit", committed.Snapshot);

        var preserved = await store.CompareExchangeAuthorityAsync(
            ActorKey("actor-1"),
            committed.Snapshot.StoreVersion,
            new ZLinkAuthorityMutation.Put(
                new byte[] { 0x33 },
                ZLinkAuthorityGenerationTransition.Preserve,
                null,
                null));
        RecordAuthority(trace, "actor-preserve", preserved);
        var preservedSnapshot =
            Assert.IsType<ZLinkAuthorityCompareExchangeResult.Stored>(preserved).Snapshot;

        RecordAuthority(
            trace,
            "actor-stale-delete",
            await store.CompareExchangeAuthorityAsync(
                ActorKey("actor-1"),
                committed.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
        RecordAuthority(
            trace,
            "actor-delete",
            await store.CompareExchangeAuthorityAsync(
                ActorKey("actor-1"),
                preservedSnapshot.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));

        var reclaimed = await ReserveActorAsync(
            store, ownerBToken, "node-b", "actor-1");
        RecordReserve(trace, "actor-reclaim", reclaimed);
        var reclaimedReservation =
            Assert.IsType<ZLinkObjectReserveResult.Reserved>(reclaimed).Reservation;
        var reclaimedCommit = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(reclaimedReservation, new byte[] { 0x44 }));
        RecordSnapshot(trace, "actor-reclaim-commit", reclaimedCommit.Snapshot);

        var second = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await ReserveActorAsync(store, ownerAToken, "node-a", "actor-2"));
        _ = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(second.Reservation, new byte[] { 0x55 }));

        trace.Add(
            $"remove-all-by-owner={await store.RemoveAllByOwnerAsync(ownerAToken)}");
        trace.Add(
            $"actor-2={GetReadKind(await store.ReadAuthorityAsync(ActorKey("actor-2")))}");
        trace.Add(
            $"actor-1={GetReadKind(await store.ReadAuthorityAsync(ActorKey("actor-1")))}");
        return trace;
    }

    private static void RecordLease(
        ICollection<string> trace,
        string step,
        ZLinkOwnerLeaseClaimResult result) =>
        trace.Add($"{step}=claimed:{result is ZLinkOwnerLeaseClaimResult.Claimed}");

    private static void RecordReserve(
        ICollection<string> trace,
        string step,
        ZLinkObjectReserveResult result)
    {
        var detail = result switch
        {
            ZLinkObjectReserveResult.Reserved reserved =>
                FormatGenerations(
                    reserved.Reservation.ObjectGeneration,
                    reserved.Reservation.AuthorityOwnerGeneration),
            ZLinkObjectReserveResult.Conflict(
                ZLinkAuthorityReadResult.Found found) =>
                FormatGenerations(
                    found.Snapshot.ObjectGeneration,
                    found.Snapshot.AuthorityOwnerGeneration),
            _ => "-"
        };
        trace.Add($"{step}={result.GetType().Name}:{detail}");
    }

    private static void RecordAuthority(
        ICollection<string> trace,
        string step,
        ZLinkAuthorityCompareExchangeResult result)
    {
        var detail = result switch
        {
            ZLinkAuthorityCompareExchangeResult.Stored stored =>
                FormatGenerations(
                    stored.Snapshot.ObjectGeneration,
                    stored.Snapshot.AuthorityOwnerGeneration),
            ZLinkAuthorityCompareExchangeResult.Conflict(
                ZLinkAuthorityReadResult.Found found) =>
                FormatGenerations(
                    found.Snapshot.ObjectGeneration,
                    found.Snapshot.AuthorityOwnerGeneration),
            _ => "-"
        };
        trace.Add($"{step}={result.GetType().Name}:{detail}");
    }

    private static void RecordSnapshot(
        ICollection<string> trace,
        string step,
        ZLinkAuthoritySnapshot snapshot) =>
        trace.Add(
            $"{step}=Committed:"
            + FormatGenerations(
                snapshot.ObjectGeneration,
                snapshot.AuthorityOwnerGeneration));

    private static async ValueTask PublishDescriptorAsync(
        IZLinkLocationStore store,
        ZLinkLocationOwnerToken owner,
        string nodeRid)
    {
        var descriptor = TestRows.MeshNode(
            owner.OwnerId,
            nodeRid: nodeRid,
            leaseGeneration: owner.LeaseGeneration);
        var result = await store.UpdateMeshNodeAsync(
            descriptor,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, result.Status);
    }

    private static ValueTask<ZLinkObjectReserveResult> ReserveActorAsync(
        IZLinkLocationStore store,
        ZLinkLocationOwnerToken owner,
        string nodeRid,
        string actorId)
    {
        var intent = System.Text.Encoding.UTF8.GetBytes($"create:{actorId}");
        return store.ReserveAsync(
            new ZLinkObjectReservationRequest(
                ZLinkPlacementObjectKind.Actor,
                ActorKey(actorId),
                "player",
                $"inline:{actorId}",
                System.Security.Cryptography.SHA256.HashData(intent),
                intent.Length,
                new ZLinkMeshNodeDescriptorKey(
                    "play",
                    RoutingId.From(nodeRid)),
                1,
                owner,
                new byte[] { 0x11 },
                new ZLinkCapacityVector(1, 0, null)));
    }

    private static ZLinkAuthorityKey ActorKey(string actorId) =>
        new($"parity:actor:{actorId}");

    private static string GetReadKind(ZLinkAuthorityReadResult result) =>
        result.GetType().Name;

    private static string FormatGenerations(
        ulong objectGeneration,
        ulong authorityOwnerGeneration) =>
        $"{objectGeneration}:{authorityOwnerGeneration}";
}
