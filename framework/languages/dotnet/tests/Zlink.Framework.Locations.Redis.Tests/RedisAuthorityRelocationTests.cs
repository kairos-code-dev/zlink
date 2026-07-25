using System.Security.Cryptography;

namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisAuthorityRelocationTests(
    RedisTestFixture fixture)
{
    [SkippableFact]
    public async Task Reserve_Writes_Exact_Hybrid_Authority_And_Schema_Hashes()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore(out var keyPrefix);
        const string ownerId = "physical-schema-owner";
        var owner = await store.ClaimLiveOwnerAsync(
            ownerId,
            TimeSpan.FromMinutes(1));
        await PublishDescriptorAsync(store, owner, RoutingId.From(ownerId));
        var key = new ZLinkAuthorityKey("zla1:a:physical-schema");

        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, "physical-schema")));

        var keys = new ZLinkRedisLocationKeys(keyPrefix);
        var current = await fixture.HashGetAllAsync(
            keys.HybridAuthorityCurrentKey(key.Value).ToString());
        Assert.Equal(
            new[]
            {
                "allocationState",
                "authorityKey",
                "authorityOwnerGeneration",
                "capacityBundle",
                "descriptorKey",
                "descriptorLifecycleGeneration",
                "objectGeneration",
                "objectKind",
                "ownerId",
                "ownerLeaseGeneration",
                "payload",
                "requestContentReference",
                "requestEncodedSize",
                "requestSha256",
                "reservationId",
                "stableType",
                "storeVersion"
            },
            current.Keys.Order(StringComparer.Ordinal));
        Assert.Equal("actor", current["objectKind"]);
        Assert.Equal(
            "24:zlink-capacity-bundle-v21:11:01:0",
            current["capacityBundle"]);
        Assert.Equal(key.Value, current["authorityKey"]);
        var pending = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(key)).Snapshot.ReservedCreation;
        Assert.NotNull(pending);
        Assert.Equal(
            reserved.Reservation.ReservationVersion,
            pending.ReservationId);
        Assert.Equal(
            InlineReference("physical-schema"u8),
            pending.RequestContentReference);
        Assert.Equal("physical-schema".Length, pending.RequestEncodedSize);
        Assert.Equal(
            SHA256.HashData("physical-schema"u8.ToArray()),
            pending.RequestSha256.ToArray());

        Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:physical-schema-sentinel"),
                    owner,
                    "physical-schema-sentinel")));
        var heldScan = Assert.IsType<ZLinkAuthorityScanResult.Page>(
            await store.ListAuthoritiesAsync("zla1:a:", null, 1));
        Assert.NotNull(heldScan.Value.NextCursor);

        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                reserved.Reservation,
                "physical-ready"u8.ToArray()));
        var active = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(key)).Snapshot;
        Assert.Null(active.ReservedCreation);
        const string revision = "0000000000000001";
        var history = await fixture.HashGetAllAsync(
            keys.HybridAuthorityHistoryKey(key.Value).ToString());
        Assert.Equal("0", history[$"{revision}:deleted"]);
        foreach (var field in current.Keys)
            Assert.Equal(current[field], history[$"{revision}:{field}"]);

        var descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(ownerId)));
        var nodeBucket = ZLinkRedisLocationKeys.HybridCapacityPopulationBucket(
            descriptorKey,
            1,
            ZLinkPlacementObjectKind.Actor);
        var typeActive = await fixture.HashGetAllAsync(
            keys.HybridCapacityKey(type: true, pending: false).ToString());
        var nodeActive = await fixture.HashGetAllAsync(
            keys.HybridCapacityKey(type: false, pending: false).ToString());
        Assert.Empty(typeActive);
        Assert.Equal("1", nodeActive[nodeBucket]);

        var schema = await fixture.HashGetAllAsync(
            keys.HybridSchemaKey().ToString());
        Assert.Equal("location-authority-hybrid-v3", schema["format"]);
        Assert.Equal("3", schema["epoch"]);
    }

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
    public async Task Rejected_Creation_Is_Replayed_Only_For_The_Same_Operation()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        const string ownerId = "creation-terminal-owner";
        var owner = await store.ClaimLiveOwnerAsync(
            ownerId,
            TimeSpan.FromMinutes(1));
        await PublishDescriptorAsync(store, owner, RoutingId.From(ownerId));
        var key = new ZLinkAuthorityKey("zla1:a:creation-terminal");
        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, "first")));
        var operation = new ZLinkCreationOperationId(
            RoutingId.From("node-a"),
            7,
            0,
            1);
        var envelope = Convert.FromHexString(
            "010000000d00000000000000000103000000");
        var terminal = new ZLinkCreationTerminalPublication(
            operation,
            envelope,
            SHA256.HashData(envelope),
            DateTimeOffset.UtcNow.AddMinutes(5));

        Assert.IsType<ZLinkObjectCreationCompleteResult.Rejected>(
            await store.CompleteCreationAsync(
                reserved.Reservation,
                new ZLinkObjectCreationCompletion.Rejected(terminal)));
        var replay = Assert.IsType<ZLinkCreationTerminalReadResult.Found>(
            await store.ReadCreationTerminalAsync(operation));
        Assert.Equal(
            ZLinkCreationTerminalState.Rejected,
            replay.Record.State);
        Assert.Equal(envelope, replay.Record.TerminalEnvelope.ToArray());

        Assert.IsType<ZLinkCreationTerminalReadResult.Missing>(
            await store.ReadCreationTerminalAsync(
                operation with { OperationIdLow = 2 }));
        Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, "second")));
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
        var owner = await store.ClaimLiveOwnerAsync(
            ownerId,
            TimeSpan.FromMinutes(1));
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
    public async Task Descriptor_Cas_And_Capacity_Projection_Are_Exact()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var ownerId = $"descriptor-owner-{Guid.NewGuid():N}";
        var claimed = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                ownerId,
                TimeSpan.FromMinutes(1)));
        var owner = claimed.Token;
        var rid = RoutingId.From(ownerId);
        var descriptor = Descriptor(owner, rid, lifecycleGeneration: 1);

        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor,
                ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateMeshNodeAsync(
                descriptor with { PlacementWeight = 75 },
                ZLinkLocationWriteIntent.Renew)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateMeshNodeAsync(
                descriptor with
                {
                    DescriptorRevision = 2,
                    ApplicationVersion = 2
                },
                ZLinkLocationWriteIntent.Renew)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor with
                {
                    DescriptorRevision = 2,
                    PlacementWeight = 75
                },
                ZLinkLocationWriteIntent.Renew)).Status);

        var key = new ZLinkAuthorityKey(
            $"zla1:a:descriptor-capacity-{Guid.NewGuid():N}");
        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, key.Value)));
        var pending = Assert.Single(
            (await store.ListMeshNodesAsync("game", default)).Items,
            value => value.Rid == rid);
        Assert.Equal(0, pending.Capacity.Actors.Active);
        Assert.Equal(1, pending.Capacity.Actors.Reserved);

        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                reserved.Reservation,
                "ready"u8.ToArray()));
        var active = Assert.Single(
            (await store.ListMeshNodesAsync("game", default)).Items,
            value => value.Rid == rid);
        Assert.Equal(1, active.Capacity.Actors.Active);
        Assert.Equal(0, active.Capacity.Actors.Reserved);
    }

    [SkippableFact]
    public async Task Aggregate_Commit_Changes_All_Owners_Atomically()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore(out var keyPrefix);
        var source = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "source-owner",
                TimeSpan.FromMinutes(1))).Token;
        var target = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "target-owner",
                TimeSpan.FromMinutes(1))).Token;
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
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From("target")),
            1,
            new ZLinkCapacityVector(keys.Length, 0, null),
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

        var sourceDescriptor = new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(source.OwnerId));
        var sourceBucket = ZLinkRedisLocationKeys.HybridCapacityPopulationBucket(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(sourceDescriptor),
            1,
            ZLinkPlacementObjectKind.Actor);
        var targetDescriptor = new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From("target"));
        var targetBucket = ZLinkRedisLocationKeys.HybridCapacityPopulationBucket(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(targetDescriptor),
            1,
            ZLinkPlacementObjectKind.Actor);
        var physicalKeys = new ZLinkRedisLocationKeys(keyPrefix);
        var activeCapacityKey = physicalKeys
            .HybridCapacityKey(type: false, pending: false)
            .ToString();
        var pendingCapacityKey = physicalKeys
            .HybridCapacityKey(type: false, pending: true)
            .ToString();
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
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(source.OwnerId)),
            1,
            new ZLinkCapacityVector(keys.Length, 0, null),
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
    public async Task Aggregate_Preserve_Normalization_Accepts_Zero_Capacity_Delta()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "preserve-owner",
                TimeSpan.FromMinutes(1))).Token;
        var nodeRid = RoutingId.From(owner.OwnerId);
        await PublishDescriptorAsync(store, owner, nodeRid);

        var key = new ZLinkAuthorityKey(
            $"zla1:a:preserve-{Guid.NewGuid():N}");
        var reservation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, owner, key.Value)));
        var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                reservation.Reservation,
                "ready"u8.ToArray()));
        var request = new ZLinkAggregatePrepareRequest(
            Guid.NewGuid(),
            1,
            [
                new ZLinkAggregateParticipant(
                    key,
                    ready.Snapshot.StoreVersion,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    "normalized"u8.ToArray(),
                    ReadOnlyMemory<byte>.Empty)
            ],
            SHA256.HashData("preserve-inventory"u8),
            new ZLinkMeshNodeDescriptorKey("game", nodeRid),
            1,
            new ZLinkCapacityVector(0, 0, null),
            owner);

        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(() =>
            store.PrepareAggregateAsync(
                    request with
                    {
                        Participants =
                        [
                            request.Participants[0] with
                            {
                                MembershipMutation = new byte[] { 0x01 }
                            }
                        ]
                    })
                .AsTask());

        var prepared = Assert.IsType<ZLinkAggregatePrepareResult.Prepared>(
            await store.PrepareAggregateAsync(request));
        Assert.Equal(
            ZLinkAggregateCommitResult.Committed,
            await store.CommitAggregateAsync(prepared.Fence));
        var normalized = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(key));
        Assert.Equal(
            "normalized"u8.ToArray(),
            normalized.Snapshot.Payload.ToArray());
        Assert.Equal(owner.OwnerId, normalized.Snapshot.OwnerId);
    }

    [SkippableFact]
    public async Task Creation_Commit_Rechecks_Target_Descriptor_And_Abort_Cleans_Pending()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var ownerId = "stale-target-owner";
        var owner = await store.ClaimLiveOwnerAsync(
            ownerId,
            TimeSpan.FromMinutes(1));
        var rid = RoutingId.From(owner.OwnerId);
        await PublishDescriptorAsync(store, owner, rid);
        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:stale-target"),
                    owner,
                    "stale-target")));

        Assert.Equal(
            ZLinkOwnerLeaseReleaseResult.Released,
            await store.ReleaseOwnerLeaseAsync(owner));
        Assert.IsType<ZLinkObjectCommitResult.Stale>(
            await store.CommitAsync(
                reserved.Reservation,
                "ready"u8.ToArray()));
        Assert.IsType<ZLinkObjectAbortResult.Aborted>(
            await store.AbortAsync(reserved.Reservation));
    }

    [SkippableFact]
    public async Task Creation_Admission_Rejects_Stable_Type_And_Capacity_Overflow()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var ownerId = "bounded-owner";
        var owner = await store.ClaimLiveOwnerAsync(
            ownerId,
            TimeSpan.FromMinutes(1));
        var rid = RoutingId.From(owner.OwnerId);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                Descriptor(
                    owner,
                    rid,
                    lifecycleGeneration: 1,
                    activeLimit: 1,
                    pendingLimit: 1),
                ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.IsType<ZLinkObjectReserveResult.Conflict>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:wrong-profile"),
                    owner,
                    "wrong-profile",
                    stableType: "Other.Actor")));
        var first = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:bounded-first"),
                    owner,
                    "bounded-first")));
        Assert.IsType<ZLinkObjectReserveResult.PlacementCapacityExhausted>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:pending-overflow"),
                    owner,
                    "pending-overflow")));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(first.Reservation, "ready"u8.ToArray()));
        Assert.IsType<ZLinkObjectReserveResult.PlacementCapacityExhausted>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:active-overflow"),
                    owner,
                    "active-overflow")));
    }

    [SkippableFact]
    public async Task Relocation_Admission_Rejects_Capability_And_Capacity_Overflow()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var source = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "relocation-source",
                TimeSpan.FromMinutes(1))).Token;
        var target = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "relocation-target",
                TimeSpan.FromMinutes(1))).Token;
        await PublishDescriptorAsync(
            store,
            source,
            RoutingId.From(source.OwnerId));
        var unavailableTarget = Descriptor(
            target,
            RoutingId.From(target.OwnerId),
            lifecycleGeneration: 1,
            activeLimit: 1,
            pendingLimit: 1) with
        {
            PlacementWeight = 0
        };
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                unavailableTarget,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        var key = new ZLinkAuthorityKey("zla1:a:relocation-source");
        var creation = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(Request(key, source, "relocation-source")));
        var ready = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(creation.Reservation, "ready"u8.ToArray()));
        var relocation = RelocationRequest(
            ready.Snapshot,
            source,
            target);
        Assert.IsType<ZLinkRelocationCapacityReserveResult.TargetUnavailable>(
            await store.ReserveRelocationCapacityAsync(relocation));

        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                unavailableTarget with
                {
                    DescriptorRevision = 2,
                    PlacementWeight = 100
                },
                ZLinkLocationWriteIntent.Renew)).Status);
        var admitted = Assert.IsType<ZLinkRelocationCapacityReserveResult.Reserved>(
            await store.ReserveRelocationCapacityAsync(
                relocation with { ReservationId = Guid.NewGuid() }));
        Assert.Equal(
            ZLinkRelocationCapacityAbortResult.Aborted,
            await store.AbortRelocationCapacityAsync(admitted.Fence));
        var occupying = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                Request(
                    new ZLinkAuthorityKey("zla1:a:target-occupying"),
                    target,
                    "target-occupying")));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                occupying.Reservation,
                "target-ready"u8.ToArray()));
        Assert.IsType<
            ZLinkRelocationCapacityReserveResult.PlacementCapacityExhausted>(
            await store.ReserveRelocationCapacityAsync(
                relocation with { ReservationId = Guid.NewGuid() }));
    }

    [SkippableFact]
    public async Task UserSpot_Capacity_Vector_Tracks_Population_And_Stable_Type()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore(out var keyPrefix);
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "spot-capacity-owner",
                TimeSpan.FromMinutes(1))).Token;
        var descriptor = SpotDescriptor(owner, RoutingId.From("spot-capacity-node"));
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor,
                ZLinkLocationWriteIntent.NewClaim)).Status);
        var descriptorKey = new ZLinkMeshNodeDescriptorKey(
            descriptor.MeshName,
            descriptor.Rid);
        var firstKey =
            Zlink.Framework.Runtime.Spots.ZLinkUserSpotAuthorityPayloadCodec
                .AuthorityKey("spot-capacity-1");
        var first = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                SpotRequest(firstKey, owner, descriptorKey, "room")));

        var keys = new ZLinkRedisLocationKeys(keyPrefix);
        var encodedDescriptor =
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(descriptorKey);
        var populationBucket =
            ZLinkRedisLocationKeys.HybridCapacityPopulationBucket(
                encodedDescriptor,
                1,
                ZLinkPlacementObjectKind.UserSpot);
        var typeBucket = ZLinkRedisLocationKeys.HybridCapacityTypeBucket(
            encodedDescriptor,
            1,
            ZLinkPlacementObjectKind.UserSpot,
            "room");
        Assert.Equal(
            1,
            await fixture.HashGetInt64Async(
                keys.HybridCapacityKey(type: false, pending: true).ToString(),
                populationBucket));
        Assert.Equal(
            1,
            await fixture.HashGetInt64Async(
                keys.HybridCapacityKey(type: true, pending: true).ToString(),
                typeBucket));

        var committed = Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(first.Reservation, "ready"u8.ToArray()));
        Assert.IsType<ZLinkObjectReserveResult.PlacementCapacityExhausted>(
            await store.ReserveAsync(
                SpotRequest(
                    Zlink.Framework.Runtime.Spots
                        .ZLinkUserSpotAuthorityPayloadCodec
                        .AuthorityKey("spot-capacity-2"),
                    owner,
                    descriptorKey,
                    "room")));
        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Deleted>(
            await store.CompareExchangeAuthorityAsync(
                firstKey,
                committed.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
        Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await store.ReserveAsync(
                SpotRequest(
                    Zlink.Framework.Runtime.Spots
                        .ZLinkUserSpotAuthorityPayloadCodec
                        .AuthorityKey("spot-capacity-2"),
                    owner,
                    descriptorKey,
                    "room")));
    }

    private static ZLinkObjectReservationRequest Request(
        ZLinkAuthorityKey key,
        ZLinkLocationOwnerToken owner,
        string identity,
        string stableType = "Game.Actor") =>
        new(
            ZLinkPlacementObjectKind.Actor,
            key,
            stableType,
            InlineReference(System.Text.Encoding.UTF8.GetBytes(identity)),
            SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(identity)),
            identity.Length,
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(owner.OwnerId)),
            1,
            owner,
            System.Text.Encoding.UTF8.GetBytes($"creating:{identity}"),
            new ZLinkCapacityVector(1, 0, null));

    private static ZLinkObjectReservationRequest SpotRequest(
        ZLinkAuthorityKey key,
        ZLinkLocationOwnerToken owner,
        ZLinkMeshNodeDescriptorKey descriptor,
        string stableType) =>
        new(
            ZLinkPlacementObjectKind.UserSpot,
            key,
            stableType,
            InlineReference(
                System.Text.Encoding.UTF8.GetBytes(key.Value)),
            SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(key.Value)),
            key.Value.Length,
            descriptor,
            1,
            owner,
            System.Text.Encoding.UTF8.GetBytes($"creating:{key.Value}"),
            new ZLinkCapacityVector(
                0,
                1,
                new ZLinkSpotTypeCapacityDelta(
                    ZLinkPlacementObjectKind.UserSpot,
                    stableType,
                    1)));

    private static string InlineReference(ReadOnlySpan<byte> payload)
    {
        var crc = Zlink.Framework.Runtime.Locations.ZLinkCrc32C
            .Compute(payload);
        var encoded = Convert.ToBase64String(payload)
            .TrimEnd('=')
            .Replace('+', '-')
            .Replace('/', '_');
        return $"inline-v1:{crc:x8}:{encoded}";
    }

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
        ulong lifecycleGeneration,
        string stableType = "Game.Actor",
        int? activeLimit = null,
        int? pendingLimit = null) =>
        new(
            "game",
            rid,
            lifecycleGeneration,
            lifecycleGeneration,
            "tcp://127.0.0.1:5555",
            new Dictionary<string, int>(),
            "test",
            owner.OwnerId,
            owner.LeaseGeneration,
            DateTimeOffset.UnixEpoch)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            EntrySpotId = $"test-entry-{Guid.NewGuid():D}",
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.Actor,
                    stableType,
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    HasSnapshotAdapter: false,
                    Limit: 0)
            ],
            State = ZLinkFrameworkRuntimeState.Serving,
            Capacity = new(
                new ZLinkPopulationCapacity(
                    0,
                    0,
                    activeLimit ?? 10_000),
                new ZLinkPopulationCapacity(0, 0, 0),
                Array.Empty<ZLinkSpotTypeCapacity>())
        };

    private static ZLinkMeshNodeDescriptor SpotDescriptor(
        ZLinkLocationOwnerToken owner,
        RoutingId rid) =>
        new(
            "game",
            rid,
            1,
            1,
            "tcp://127.0.0.1:5556",
            new Dictionary<string, int>(),
            "test",
            owner.OwnerId,
            owner.LeaseGeneration,
            DateTimeOffset.UnixEpoch)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            EntrySpotId = $"test-entry-{Guid.NewGuid():D}",
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.UserSpot,
                    "room",
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    HasSnapshotAdapter: false,
                    Limit: 1)
            ],
            State = ZLinkFrameworkRuntimeState.Serving,
            Capacity = new(
                new ZLinkPopulationCapacity(0, 0, 0),
                new ZLinkPopulationCapacity(0, 0, 2),
                [
                    new ZLinkSpotTypeCapacity(
                        ZLinkPlacementObjectKind.UserSpot,
                        "room",
                        0,
                        0,
                        1)
                ])
        };

    private static ZLinkRelocationCapacityReservationRequest RelocationRequest(
        ZLinkAuthoritySnapshot source,
        ZLinkLocationOwnerToken sourceOwner,
        ZLinkLocationOwnerToken targetOwner) =>
        new(
            Guid.NewGuid(),
            new ZLinkAuthorityKey("zla1:a:relocation-source"),
            source.StoreVersion,
            source.Allocation.ObjectKind,
            source.Allocation.StableType,
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(sourceOwner.OwnerId)),
            1,
            sourceOwner,
            new ZLinkMeshNodeDescriptorKey(
                "game",
                RoutingId.From(targetOwner.OwnerId)),
            1,
            targetOwner,
            source.Allocation.Capacity);
}
