using System.Security.Cryptography;

namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisAuthorityRelocationTests(
    RedisTestFixture fixture)
{
    [Fact]
    public void Official_Providers_Expose_Separate_Store_Capabilities()
    {
        Assert.Contains(
            typeof(IZLinkAuthorityStore),
            typeof(ZLinkRedisLocationStore).GetInterfaces());
        Assert.Contains(
            typeof(IZLinkRelocationStore),
            typeof(ZLinkRedisRelocationStore).GetInterfaces());
        Assert.DoesNotContain(
            typeof(IZLinkRelocationStore),
            typeof(ZLinkRedisLocationStore).GetInterfaces());
    }

    [SkippableFact]
    public async Task Relocation_Payload_Is_Content_Addressed_And_Immutable()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        var prefix =
            $"{fixture.RunKeyPrefix}:relocation:{Guid.NewGuid():N}";
        await using var store = new ZLinkRedisRelocationStore(
            new ZLinkRedisRelocationOptions
            {
                ConnectionString = fixture.ConnectionString,
                KeyPrefix = prefix
            });
        var payload = "immutable relocation payload"u8.ToArray();

        var first = await store.PutRelocationAsync(
            payload,
            TimeSpan.FromMinutes(5));
        var second = await store.PutRelocationAsync(
            payload,
            TimeSpan.FromMinutes(10));

        Assert.Equal(
            Convert.ToHexString(SHA256.HashData(payload)).ToLowerInvariant(),
            first.Reference);
        Assert.Equal(first.Reference, second.Reference);
        Assert.Equal(first.ChecksumCrc32c, second.ChecksumCrc32c);
        var found = Assert.IsType<ZLinkRelocationReadResult.Found>(
            await store.GetRelocationAsync(first.Reference));
        Assert.Equal(payload, found.Payload.ToArray());
        Assert.IsType<ZLinkRelocationRenewResult.Renewed>(
            await store.RenewRelocationAsync(
                first.Reference,
                TimeSpan.FromMinutes(15)));
        Assert.Equal(
            ZLinkRelocationDeleteResult.Deleted,
            await store.DeleteRelocationAsync(first.Reference));
        Assert.IsType<ZLinkRelocationReadResult.Missing>(
            await store.GetRelocationAsync(first.Reference));
    }

    [SkippableFact]
    public async Task Authority_Reserve_Commit_Preserve_And_Scan_Use_Store_Fences()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        const string ownerId = "authority-owner-a";
        await store.RenewOwnerLeaseAsync(
            ownerId,
            RoutingId.From("node-a"),
            TimeSpan.FromMinutes(1));
        var owner = new ZLinkLocationOwnerToken(ownerId, 1);
        var key = new ZLinkAuthorityKey("zla1:a:actor-a");

        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, "actor-a")));
        var committed = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                reserved.Reservation,
                "ready"u8.ToArray()));
        Assert.Equal(ownerId, committed.Snapshot.OwnerId);
        Assert.Equal(1UL, committed.Snapshot.ObjectGeneration);
        Assert.Equal(1UL, committed.Snapshot.AuthorityOwnerGeneration);

        var preserved = Assert.IsType<
            ZLinkAuthorityCompareExchangeResult.Stored>(
            await store.CompareExchangeAuthorityAsync(
                key,
                new ZLinkAuthorityExpectation.Found(
                    committed.Snapshot.StoreVersion),
                new ZLinkAuthorityMutation.Put(
                    "updated"u8.ToArray(),
                    ZLinkAuthorityGenerationTransition.Preserve)));
        Assert.Equal(
            committed.Snapshot.ObjectGeneration,
            preserved.Snapshot.ObjectGeneration);
        Assert.Equal(
            committed.Snapshot.AuthorityOwnerGeneration,
            preserved.Snapshot.AuthorityOwnerGeneration);

        var page = Assert.IsType<ZLinkAuthorityScanResult.Page>(
            await store.ListAuthoritiesAsync("zla1:a:", null, 10));
        var entry = Assert.Single(page.Value.Items);
        Assert.Equal(key, entry.Key);
        Assert.Equal("updated"u8.ToArray(), entry.Snapshot.Payload.ToArray());
    }

    [SkippableFact]
    public async Task Aggregate_Commit_Changes_All_Owners_Atomically()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var source = new ZLinkLocationOwnerToken("source-owner", 1);
        var target = new ZLinkLocationOwnerToken("target-owner", 2);
        await store.RenewOwnerLeaseAsync(
            source.OwnerId,
            RoutingId.From("source"),
            TimeSpan.FromMinutes(1));
        await store.RenewOwnerLeaseAsync(
            target.OwnerId,
            RoutingId.From("target"),
            TimeSpan.FromMinutes(1));
        var keys = new[]
        {
            new ZLinkAuthorityKey("zla1:s:spot-a"),
            new ZLinkAuthorityKey("zla1:a:actor-a")
        };
        var snapshots = new List<ZLinkAuthoritySnapshot>();
        foreach (var key in keys)
        {
            var reservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
                await store.ReserveAsync(
                    Request(key, source, key.Value)));
            snapshots.Add(Assert.IsType<ZLinkObjectCommitResult.Committed>(
                await store.CommitAsync(
                    reservation.Reservation,
                    "ready"u8.ToArray())).Snapshot);
        }

        var aggregateId = Guid.NewGuid();
        var prepare = Assert.IsType<ZLinkAggregatePrepareResult.Prepared>(
            await store.PrepareAggregateAsync(
                new ZLinkAggregatePrepareRequest(
                    aggregateId,
                    1,
                    keys.Select((key, index) =>
                        new ZLinkAggregateParticipant(
                            key,
                            snapshots[index].StoreVersion,
                            ZLinkAuthorityGenerationTransition.NewOwner,
                            System.Text.Encoding.UTF8.GetBytes(
                                $"payload-{index}"),
                            System.Text.Encoding.UTF8.GetBytes(
                                $"membership-{index}")))
                        .ToArray(),
                    SHA256.HashData("inventory"u8),
                    [],
                    target)));

        Assert.Equal(
            ZLinkAggregateCommitResult.Committed,
            await store.CommitAggregateAsync(prepare.Fence));
        foreach (var key in keys)
        {
            var found = Assert.IsType<ZLinkAuthorityReadResult.Found>(
                await store.ReadAuthorityAsync(key));
            Assert.Equal(target.OwnerId, found.Snapshot.OwnerId);
            Assert.Equal(target.Generation, checked((ulong)found.Snapshot.OwnerLeaseGeneration));
        }
    }

    private static ZLinkObjectReservationRequest Request(
        ZLinkAuthorityKey key,
        ZLinkLocationOwnerToken owner,
        string identity) =>
        new(
            ZLinkPlacementObjectKind.Actor,
            key,
            "Game.Actor",
            null,
            null,
            $"intent:{identity}",
            SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(identity)),
            identity.Length,
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(owner.OwnerId)),
            owner,
            1);
}
