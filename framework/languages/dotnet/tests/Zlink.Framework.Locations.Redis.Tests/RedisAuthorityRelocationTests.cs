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
        await PublishDescriptorAsync(
            store,
            owner,
            RoutingId.From(owner.OwnerId));
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
                committed.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    "updated"u8.ToArray(),
                    ZLinkAuthorityGenerationTransition.Preserve,
                    null,
                    null)));
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
        await using var store = fixture.CreateStore(out var keyPrefix);
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
        await PublishDescriptorAsync(
            store,
            source,
            RoutingId.From(source.OwnerId));
        await PublishDescriptorAsync(
            store,
            target,
            RoutingId.From("target"));
        var keys = new[]
        {
            new ZLinkAuthorityKey("zla1:a:actor-a"),
            new ZLinkAuthorityKey("zla1:s:spot-a")
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
        var capacityFences = new List<ZLinkRelocationCapacityFence>();
        for (var index = 0; index < keys.Length; index++)
        {
            var capacity = Assert.IsType<
                ZLinkRelocationCapacityReserveResult.Reserved>(
                await store.ReserveRelocationCapacityAsync(
                    new ZLinkRelocationCapacityReservationRequest(
                        Guid.NewGuid(),
                        keys[index],
                        snapshots[index].StoreVersion,
                        ZLinkPlacementObjectKind.Actor,
                        "Game.Actor",
                        new ZLinkMeshNodeDescriptorKey(
                            "game",
                            RoutingId.From(source.OwnerId)),
                        1,
                        source,
                        new ZLinkMeshNodeDescriptorKey(
                            "game",
                            RoutingId.From("target")),
                        1,
                        target,
                        1)));
            capacityFences.Add(capacity.Fence);
        }
        var request = new ZLinkAggregatePrepareRequest(
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
            capacityFences,
            target);
        var prepare = Assert.IsType<ZLinkAggregatePrepareResult.Prepared>(
            await store.PrepareAggregateAsync(request));
        Assert.IsType<ZLinkAggregatePrepareResult.AlreadyPrepared>(
            await store.PrepareAggregateAsync(request));
        var changedParticipants = request.Participants.ToArray();
        changedParticipants[0] = changedParticipants[0] with
        {
            AuthorityPayload = "changed"u8.ToArray()
        };
        Assert.IsType<ZLinkAggregatePrepareResult.Conflict>(
            await store.PrepareAggregateAsync(
                request with { Participants = changedParticipants }));

        var sourceBucket = CapacityBucket(
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(source.OwnerId)),
            1,
            ZLinkPlacementObjectKind.Actor,
            "Game.Actor");
        var targetBucket = CapacityBucket(
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From("target")),
            1,
            ZLinkPlacementObjectKind.Actor,
            "Game.Actor");
        var activeCapacityKey =
            $"{keyPrefix}:authority:active-capacity";
        var pendingCapacityKey =
            $"{keyPrefix}:authority:pending-capacity";
        Assert.Equal(
            2,
            await fixture.HashGetInt64Async(
                activeCapacityKey,
                sourceBucket));
        Assert.Equal(
            2,
            await fixture.HashGetInt64Async(
                pendingCapacityKey,
                targetBucket));

        await fixture.HashSetAsync(activeCapacityKey, sourceBucket, 1);
        Assert.Equal(
            ZLinkAggregateCommitResult.Stale,
            await store.CommitAggregateAsync(prepare.Fence));
        await AssertOwnersAsync(store, keys, source.OwnerId);

        await fixture.HashSetAsync(activeCapacityKey, sourceBucket, 2);
        await fixture.HashSetAsync(pendingCapacityKey, targetBucket, 1);
        Assert.Equal(
            ZLinkAggregateCommitResult.Stale,
            await store.CommitAggregateAsync(prepare.Fence));
        await AssertOwnersAsync(store, keys, source.OwnerId);

        await fixture.HashSetAsync(pendingCapacityKey, targetBucket, 2);
        Assert.Equal(
            ZLinkAggregateCommitResult.Committed,
            await store.CommitAggregateAsync(prepare.Fence));
        await AssertOwnersAsync(
            store,
            keys,
            target.OwnerId,
            target.LeaseGeneration);

        var movedSnapshots = new List<ZLinkAuthoritySnapshot>();
        foreach (var key in keys)
            movedSnapshots.Add(
                Assert.IsType<ZLinkAuthorityReadResult.Found>(
                    await store.ReadAuthorityAsync(key)).Snapshot);
        var reverseReservations =
            new List<ZLinkRelocationCapacityFence>();
        for (var index = 0; index < keys.Length; index++)
        {
            var reverse = Assert.IsType<
                ZLinkRelocationCapacityReserveResult.Reserved>(
                await store.ReserveRelocationCapacityAsync(
                    new ZLinkRelocationCapacityReservationRequest(
                        Guid.NewGuid(),
                        keys[index],
                        movedSnapshots[index].StoreVersion,
                        ZLinkPlacementObjectKind.Actor,
                        "Game.Actor",
                        new ZLinkMeshNodeDescriptorKey(
                            "game",
                            RoutingId.From("target")),
                        1,
                        target,
                        new ZLinkMeshNodeDescriptorKey(
                            "game",
                            RoutingId.From(source.OwnerId)),
                        1,
                        source,
                        1)));
            reverseReservations.Add(reverse.Fence);
        }
        var reverseRequest = new ZLinkAggregatePrepareRequest(
            Guid.NewGuid(),
            1,
            keys.Select((key, index) =>
                new ZLinkAggregateParticipant(
                    key,
                    movedSnapshots[index].StoreVersion,
                    ZLinkAuthorityGenerationTransition.NewOwner,
                    System.Text.Encoding.UTF8.GetBytes(
                        $"reverse-payload-{index}"),
                    System.Text.Encoding.UTF8.GetBytes(
                        $"reverse-membership-{index}")))
                .ToArray(),
            SHA256.HashData("reverse-inventory"u8),
            reverseReservations,
            source);
        var reversePrepared =
            Assert.IsType<ZLinkAggregatePrepareResult.Prepared>(
                await store.PrepareAggregateAsync(reverseRequest));
        Assert.Equal(
            2,
            await fixture.HashGetInt64Async(
                pendingCapacityKey,
                sourceBucket));

        await fixture.HashSetAsync(pendingCapacityKey, sourceBucket, 1);
        Assert.Equal(
            ZLinkAggregateAbortResult.Stale,
            await store.AbortAggregateAsync(reversePrepared.Fence));
        await AssertOwnersAsync(store, keys, target.OwnerId);

        await fixture.HashSetAsync(pendingCapacityKey, sourceBucket, 2);
        Assert.Equal(
            ZLinkAggregateAbortResult.Aborted,
            await store.AbortAggregateAsync(reversePrepared.Fence));
        Assert.Equal(
            0,
            await fixture.HashGetInt64Async(
                pendingCapacityKey,
                sourceBucket));
        await AssertOwnersAsync(store, keys, target.OwnerId);
    }

    [SkippableFact]
    public async Task Creation_Commit_Rechecks_Target_Descriptor_And_Abort_Cleans_Pending()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = new ZLinkLocationOwnerToken("stale-target-owner", 1);
        var rid = RoutingId.From(owner.OwnerId);
        await store.RenewOwnerLeaseAsync(
            owner.OwnerId,
            rid,
            TimeSpan.FromMinutes(1));
        await PublishDescriptorAsync(store, owner, rid);
        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:stale-target"),
                    owner,
                    "stale-target")));

        var replacement = await store.UpdateMeshNodeAsync(
            Descriptor(owner, rid, lifecycleGeneration: 2),
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, replacement.Status);
        Assert.IsType<ZLinkObjectCommitResult.Stale>(
            await store.CommitAsync(
                reserved.Reservation,
                "ready"u8.ToArray()));
        Assert.IsType<ZLinkObjectAbortResult.Aborted>(
            await store.AbortAsync(reserved.Reservation));
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
            1,
            owner,
            System.Text.Encoding.UTF8.GetBytes($"creating:{identity}"),
            1);

    private static async Task PublishDescriptorAsync(
        ZLinkRedisLocationStore store,
        ZLinkLocationOwnerToken owner,
        RoutingId rid)
    {
        var result = await store.UpdateMeshNodeAsync(
            Descriptor(owner, rid, lifecycleGeneration: 1),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, result.Status);
    }

    private static async Task AssertOwnersAsync(
        ZLinkRedisLocationStore store,
        IEnumerable<ZLinkAuthorityKey> keys,
        string expectedOwnerId,
        long? expectedLeaseGeneration = null)
    {
        foreach (var key in keys)
        {
            var found = Assert.IsType<ZLinkAuthorityReadResult.Found>(
                await store.ReadAuthorityAsync(key));
            Assert.Equal(expectedOwnerId, found.Snapshot.OwnerId);
            if (expectedLeaseGeneration is { } generation)
                Assert.Equal(
                    generation,
                    found.Snapshot.OwnerLeaseGeneration);
        }
    }

    private static string CapacityBucket(
        ZLinkMeshNodeDescriptorKey descriptor,
        ulong lifecycleGeneration,
        ZLinkPlacementObjectKind objectKind,
        string stableType)
    {
        var mesh = descriptor.MeshName;
        var rid = descriptor.Rid.ToHex();
        var lifecycle = lifecycleGeneration.ToString(
            System.Globalization.CultureInfo.InvariantCulture);
        var kind = ((int)objectKind).ToString(
            System.Globalization.CultureInfo.InvariantCulture);
        return $"{mesh.Length}:{mesh}"
               + $"{rid.Length}:{rid}"
               + $"{lifecycle.Length}:{lifecycle}"
               + $"{kind.Length}:{kind}"
               + $"{stableType.Length}:{stableType}";
    }

    private static ZLinkMeshNodeDescriptor Descriptor(
        ZLinkLocationOwnerToken owner,
        RoutingId rid,
        ulong lifecycleGeneration) =>
        new(
            "game",
            rid,
            lifecycleGeneration,
            lifecycleGeneration,
            "tcp://127.0.0.1:5555",
            new Dictionary<string, int>(),
            new HashSet<string>(StringComparer.Ordinal),
            false,
            "test",
            owner.OwnerId,
            DateTimeOffset.UnixEpoch);
}
