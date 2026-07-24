namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisLocationStoreTests
{
    private const string OwnerA = "owner-a";
    private const string OwnerB = "owner-b";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(30);

    private readonly RedisTestFixture _fixture;

    public RedisLocationStoreTests(RedisTestFixture fixture)
    {
        _fixture = fixture;
    }

    [Fact]
    public void ActorRef_Row_Json_Uses_Typed_Internal_Codec()
    {
        var json = ZLinkRedisLocationRowJson.Serialize(TestRows.Actor(OwnerA));

        Assert.Contains("\"ActorRef\":{", json, StringComparison.Ordinal);
        Assert.Contains("\"nodeRid\":", json, StringComparison.Ordinal);
        Assert.Contains("\"actorId\":\"actor-1\"", json, StringComparison.Ordinal);
        Assert.Contains("\"generation\":1", json, StringComparison.Ordinal);
        Assert.DoesNotContain("node-1:1:actor-1", json, StringComparison.Ordinal);
        Assert.Contains("\"SpotKind\":", json, StringComparison.Ordinal);
        Assert.DoesNotContain("LocationKind", json, StringComparison.Ordinal);

        var roundtrip = ZLinkRedisLocationRowJson.Deserialize<ZLinkActorLocation>(json);
        Assert.Equal(RoutingId.From("node-1"), roundtrip.ActorRef.NodeRid);
        Assert.Equal("actor-1", roundtrip.ActorRef.ActorId);
        Assert.Equal(1ul, roundtrip.ActorRef.Generation);
    }

    [Fact]
    public void WeightJsonAcceptsSignedMaximumAndRejectsOutOfRangeValues()
    {
        var maximum = TestRows.MeshNode(OwnerA) with
        {
            PlacementWeight = 10_000,
            ChannelWeights = new Dictionary<string, int>(
                StringComparer.Ordinal)
            {
                ["play"] = 10_000,
                ["excluded"] = 0
            }
        };

        var json = ZLinkRedisLocationRowJson.Serialize(maximum);
        var decoded =
            ZLinkRedisLocationRowJson.Deserialize<ZLinkMeshNodeDescriptor>(
                json);
        Assert.Equal(10_000, decoded.PlacementWeight);
        Assert.Equal(10_000, decoded.ChannelWeights["play"]);
        Assert.Equal(0, decoded.ChannelWeights["excluded"]);

        Assert.Throws<System.Text.Json.JsonException>(() =>
            ZLinkRedisLocationRowJson.Serialize(
                maximum with { PlacementWeight = 10_001 }));
        Assert.Throws<System.Text.Json.JsonException>(() =>
            ZLinkRedisLocationRowJson.Serialize(
                maximum with
                {
                    ChannelWeights =
                        new Dictionary<string, int>(StringComparer.Ordinal)
                        {
                            ["play"] = -1
                        }
                }));
    }

    [Fact]
    public void Descriptor_Row_Json_Requires_The_Complete_Exact_Contract()
    {
        var row = TestRows.MeshNode(OwnerA) with
        {
            LifecycleGeneration = 1,
            State = ZLinkFrameworkRuntimeState.Draining
        };
        var json = ZLinkRedisLocationRowJson.Serialize(row);

        Assert.Contains("\"State\":3", json, StringComparison.Ordinal);
        Assert.Contains("\"LeaseGeneration\":1", json, StringComparison.Ordinal);
        Assert.Equal(
            ZLinkFrameworkRuntimeState.Draining,
            ZLinkRedisLocationRowJson.Deserialize<ZLinkMeshNodeDescriptor>(json).State);

        var legacyObject = System.Text.Json.Nodes.JsonNode.Parse(json)!.AsObject();
        Assert.True(legacyObject.Remove("ObjectCapabilities"));
        var legacy = legacyObject.ToJsonString();
        Assert.Throws<System.Text.Json.JsonException>(
            () => ZLinkRedisLocationRowJson.Deserialize<ZLinkMeshNodeDescriptor>(legacy));
    }

    [SkippableFact]
    public async Task OwnerLease_And_Descriptor_Use_Only_Canonical_Hybrid_Records()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore(out var prefix);
        await store.RenewOwnerLeaseAsync(
            OwnerA,
            RoutingId.From("node-1"),
            LeaseTtl);
        var token = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;

        var keys = new ZLinkRedisLocationKeys(prefix);
        var leaseKey = keys.HybridOwnerLeaseKey(OwnerA).ToString();
        Assert.Equal(
            new[] { "expiresAt", "generation", "ownerId" },
            (await _fixture.HashGetAllAsync(leaseKey)).Keys
            .Order(StringComparer.Ordinal));
        Assert.True((await _fixture.KeyTimeToLiveAsync(leaseKey)) > TimeSpan.Zero);
        Assert.False(await _fixture.KeyExistsAsync($"{prefix}:lease:{OwnerA}"));

        var descriptor = TestRows.MeshNode(
            OwnerA,
            leaseGeneration: token.LeaseGeneration);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        var canonicalKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            new ZLinkMeshNodeDescriptorKey(
                descriptor.MeshName,
                descriptor.Rid));
        var descriptorKey = keys.HybridDescriptorKey(canonicalKey).ToString();
        Assert.Equal(
            new[] { "gen", "json", "mesh", "owner", "updatedAtMs" },
            (await _fixture.HashGetAllAsync(descriptorKey)).Keys
            .Order(StringComparer.Ordinal));
        Assert.Equal(
            new[]
            {
                "actorLimit",
                "applicationVersion",
                "capabilities",
                "descriptorKey",
                "descriptorRevision",
                "immutableDigest",
                "lifecycleGeneration",
                "objectRole",
                "ownerId",
                "ownerLeaseGeneration",
                "runtimeState",
                "spotLimit"
            },
            (await _fixture.HashGetAllAsync(
                keys.HybridDescriptorAdmissionKey(canonicalKey).ToString()))
            .Keys
            .Order(StringComparer.Ordinal));

        var descriptorBase = descriptorKey[..(descriptorKey.LastIndexOf(':') + 1)];
        Assert.Contains(
            canonicalKey,
            await _fixture.SetMembersAsync(descriptorBase + "index"));
        var ownerTokenBytes = System.Text.Encoding.UTF8.GetBytes(
            OwnerA
            + "\0"
            + token.LeaseGeneration.ToString(
                System.Globalization.CultureInfo.InvariantCulture));
        var ownerTokenDigest = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(ownerTokenBytes))
            .ToLowerInvariant();
        Assert.Contains(
            canonicalKey,
            await _fixture.SetMembersAsync(
                descriptorBase + "owner:" + ownerTokenDigest));
    }

    [Fact]
    public void RemoveAllByOwner_Uses_One_Lua_Script_For_All_Kinds()
    {
        Assert.Contains("for i = 1, 4 do", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.Contains("local ownerIndex = KEYS[i]", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.Contains("redis.call('DEL', ownerIndex)", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.DoesNotContain("MULTI", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.OrdinalIgnoreCase);
    }

    [SkippableFact]
    public async Task Update_Resolve_Remove_Roundtrip_Preserves_Row_Fields()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        var claimed = await store.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);
        Assert.Equal(1UL, claimed.Generation);
        Assert.NotEqual(default, claimed.UpdatedAt);

        var resolved = await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1"));
        Assert.NotNull(resolved);
        Assert.Equal("actor-1", resolved!.ActorRef.ActorId);
        Assert.Equal(RoutingId.From("node-1"), resolved.OwnerNodeRid);
        Assert.Equal(ZLinkSpotKind.Entry, resolved.SpotKind);
        Assert.Equal(string.Empty, resolved.SpotId);
        Assert.Equal(OwnerA, resolved.OwnerId);
        // UpdatedAt comes from the store clock, not from the row the writer
        // sent (it carried a default timestamp).
        Assert.Equal(claimed.UpdatedAt, resolved.UpdatedAt);

        var removed = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
    }

    [SkippableFact]
    public async Task Descriptor_Payloads_Roundtrip_Structured_Fields()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var descriptors = await store.ListMeshNodesAsync("play");
        var descriptor = Assert.Single(descriptors);
        Assert.Equal("tcp://127.0.0.1:5001", descriptor.Endpoint);
        Assert.Equal(100, descriptor.ChannelWeights["play"]);
        Assert.Equal(50, descriptor.ChannelWeights["world"]);
        Assert.Equal("cluster-a", descriptor.SecurityIdentity);
        Assert.Equal(ZLinkFrameworkRuntimeState.Serving, descriptor.State);
        Assert.Equal(1, descriptor.LeaseGeneration);
        Assert.Equal(7, descriptor.ApplicationVersion);
        Assert.Equal(ZLinkMeshNodeObjectRole.Server, descriptor.ObjectRole);
        Assert.Equal(80, descriptor.PlacementWeight);
        Assert.Equal(
            new ZLinkPopulationCapacity(0, 0, 1_000),
            descriptor.Capacity.Actors);
        Assert.Equal(
            new ZLinkPopulationCapacity(0, 0, 1_000),
            descriptor.Capacity.Spots);
        Assert.Empty(descriptor.Capacity.SpotTypes);
        Assert.Equal("wave-a", descriptor.MaintenanceWave);
        var capability = Assert.Single(descriptor.ObjectCapabilities);
        Assert.Equal(ZLinkPlacementObjectKind.Actor, capability.ObjectKind);
        Assert.Equal("player", capability.StableType);
        Assert.Equal(0, capability.Limit);
    }

    [SkippableFact]
    public async Task Concurrent_NewClaim_From_Two_Owners_Stores_Exactly_One()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;

        for (var attempt = 0; attempt < 10; attempt++)
        {
            var actorId = $"race-{attempt}";
            var claims = await Task.WhenAll(
                store.UpdateActorAsync(TestRows.Actor(OwnerA, actorId), ZLinkLocationWriteIntent.NewClaim).AsTask(),
                store.UpdateActorAsync(TestRows.Actor(OwnerB, actorId), ZLinkLocationWriteIntent.NewClaim).AsTask());

            Assert.Equal(1, claims.Count(result => result.Status == ZLinkLocationWriteStatus.Stored));
            Assert.Equal(1, claims.Count(result => result.Status == ZLinkLocationWriteStatus.RejectedConflict));
        }
    }

    [SkippableFact]
    public async Task Generation_Strictly_Increases_Across_Remove_And_Reclaim()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;

        var first = await store.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);
        Assert.Equal(1UL, first.Generation);

        var removed = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, first.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);

        // Generation counters survive row removal, so a re-claim can never
        // reuse an old fencing token.
        var reclaimed = await store.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, reclaimed.Status);
        Assert.Equal(2UL, reclaimed.Generation);
    }

    [SkippableFact]
    public async Task Renew_Requires_Current_Owner_And_Keeps_Generation()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        var claimed = await store.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        var renewed = await store.UpdateActorAsync(
            TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(claimed.Generation, renewed.Generation);

        var wrongOwner = await store.UpdateActorAsync(
            TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, wrongOwner.Status);
    }

    [SkippableFact]
    public async Task Takeover_Replaces_Live_Row_And_Old_Owner_Writes_Become_Stale()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        var claimed = await store.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);

        var takeover = await store.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.True(takeover.Generation > claimed.Generation);

        // The replaced owner learns it lost ownership from its next write.
        var staleRenew = await store.UpdateActorAsync(
            TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRenew.Status);

        var staleRemove = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("play", "actor-1"),
            new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRemove);
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
    }

    [SkippableFact]
    public async Task Owner_Lease_Expiry_Makes_Rows_Claimable_Via_NewClaim()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), TimeSpan.FromMilliseconds(300));
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), LeaseTtl);

        var first = await store.UpdateActorAsync(TestRows.Actor(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, first.Status);

        var conflict = await store.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, conflict.Status);

        // Owner A stops heartbeating; the Redis PX TTL expires the lease and
        // A's rows become claimable without any row write.
        var reclaimed = await WaitForStoredWriteAsync(
            () => store.UpdateActorAsync(TestRows.Actor(OwnerB), ZLinkLocationWriteIntent.NewClaim));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, reclaimed.Status);
        Assert.Equal(2UL, reclaimed.Generation);
    }

    [SkippableFact]
    public async Task RemoveByOwner_Bulk_Removes_Only_That_Owners_Rows()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        IZLinkActorLocationStore actors = store;
        await actors.UpdateActorAsync(TestRows.Actor(OwnerA, "actor-1"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(TestRows.Actor(OwnerA, "actor-2"), ZLinkLocationWriteIntent.NewClaim);
        await actors.UpdateActorAsync(TestRows.Actor(OwnerB, "actor-3"), ZLinkLocationWriteIntent.NewClaim);

        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;
        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(2, removed);
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-2")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-3")));
    }

    [SkippableFact]
    public async Task RemoveAllByOwner_Removes_All_Kinds_For_The_Owner()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;
        var ownerBToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerB)).Token;

        await store.UpdateMeshNodeAsync(
            TestRows.MeshNode(
                OwnerA,
                endpoint: "tcp://127.0.0.1:5001",
                nodeRid: "node-a",
                leaseGeneration: ownerAToken.LeaseGeneration),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateMeshNodeAsync(
            TestRows.MeshNode(
                OwnerB,
                endpoint: "tcp://127.0.0.1:5002",
                nodeRid: "node-b",
                leaseGeneration: ownerBToken.LeaseGeneration),
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateSpotAsync(TestRows.Spot(OwnerB, "spot-b"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(TestRows.Actor(OwnerA, "actor-a"), ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateActorAsync(TestRows.Actor(OwnerB, "actor-b"), ZLinkLocationWriteIntent.NewClaim);

        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(3, removed);
        Assert.DoesNotContain(await store.ListMeshNodesAsync("play"), row => row.OwnerId == OwnerA);
        Assert.Contains(await store.ListMeshNodesAsync("play"), row => row.OwnerId == OwnerB);
        Assert.Null(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("spot-a")));
        Assert.NotNull(await store.ResolveSpotAsync(new ZLinkSpotLocationKey("spot-b")));
        Assert.Null(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-a")));
        Assert.NotNull(await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-b")));
    }

    [SkippableFact]
    public async Task Spot_Update_Resolve_Remove_Roundtrip_Preserves_Row_Fields()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        var claimed = await store.UpdateSpotAsync(TestRows.Spot(OwnerA, "spot-1"), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);

        var key = new ZLinkSpotLocationKey("spot-1");
        var resolved = await store.ResolveSpotAsync(key);
        Assert.NotNull(resolved);
        Assert.Equal("game", resolved!.SpotType);
        Assert.Equal(RoutingId.From("node-1"), resolved.OwnerNodeRid);
        Assert.Equal(ZLinkSpotKind.User, resolved.SpotKind);
        Assert.Equal(OwnerA, resolved.OwnerId);

        var removed = await store.RemoveSpotAsync(key, new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);
        Assert.Null(await store.ResolveSpotAsync(key));
    }

    [SkippableFact]
    public async Task MeshNode_Remove_With_Owner_Token_Deletes_The_Row()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        var claimed = await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);
        var key = new ZLinkMeshNodeDescriptorKey("play", RoutingId.From("node-1"));

        // A stale token must not delete the live row; the current one must.
        var stale = await store.RemoveMeshNodeAsync(key, new ZLinkLocationOwnerToken(OwnerA, claimed.Generation + 5));
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale);
        Assert.Single(await store.ListMeshNodesAsync("play"));

        var removed = await store.RemoveMeshNodeAsync(key, new ZLinkLocationOwnerToken(OwnerA, claimed.Generation));
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);
        Assert.Empty(await store.ListMeshNodesAsync("play"));
    }

    [SkippableFact]
    public async Task Change_Stamp_Increments_On_Writes_And_Is_Stable_On_Reads()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;
        var meshScope = new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play");
        var kindScope = new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, null);

        var beforeMesh = await store.GetChangeStampAsync(meshScope);
        var beforeKind = await store.GetChangeStampAsync(kindScope);

        var claimed = await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);

        var afterWriteMesh = await store.GetChangeStampAsync(meshScope);
        var afterWriteKind = await store.GetChangeStampAsync(kindScope);
        // Every successful write bumps both the mesh scope and the
        // kind-wide scope, exactly like the in-memory store.
        Assert.True(afterWriteMesh > beforeMesh);
        Assert.True(afterWriteKind > beforeKind);

        await store.ListMeshNodesAsync("play");
        await store.GetChangeStampAsync(meshScope);
        Assert.Equal(afterWriteMesh, await store.GetChangeStampAsync(meshScope));
        Assert.Equal(afterWriteKind, await store.GetChangeStampAsync(kindScope));

        // A failed write leaves the stamps untouched.
        var stale = await store.UpdateMeshNodeAsync(
            TestRows.MeshNode("wrong-owner"),
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale.Status);
        Assert.Equal(afterWriteMesh, await store.GetChangeStampAsync(meshScope));
    }

    [SkippableFact]
    public async Task Owner_Lease_Snapshot_Reports_StoreNow_And_Omits_Expired_Leases()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.RenewOwnerLeaseAsync(OwnerB, RoutingId.From("node-2"), TimeSpan.FromMilliseconds(300));

        var snapshot = await WaitForOwnerLeaseToExpireAsync(store, OwnerB);

        var lease = Assert.Single(snapshot.Leases);
        Assert.Equal(OwnerA, lease.OwnerId);
        Assert.Equal(RoutingId.From("node-1"), lease.NodeRid);
        Assert.NotEqual(default, snapshot.StoreNow);
        // Expiry is derived from StoreNow plus the remaining Redis TTL; the
        // application wall clock never participates in the comparison.
        Assert.True(lease.LeaseExpiresAt > snapshot.StoreNow);
        Assert.True(lease.LeaseExpiresAt - snapshot.StoreNow <= LeaseTtl);
        Assert.True(lease.UpdatedAt <= snapshot.StoreNow);

        var removed = await store.RemoveOwnerLeaseAsync(OwnerA);
        Assert.True(removed);
        Assert.Empty((await store.ListOwnerLeasesAsync()).Leases);
    }

    [Fact]
    public void Every_Write_Operation_Has_One_Atomic_Lua_Script()
    {
        var scripts = new[]
        {
            ZLinkRedisLocationScripts.Write,
            ZLinkRedisLocationScripts.Remove,
            ZLinkRedisLocationScripts.RemoveAllByOwner,
            ZLinkRedisLocationScripts.RenewLease,
            ZLinkRedisLocationScripts.RemoveLease
        };

        Assert.All(scripts, script =>
        {
            Assert.Contains("redis.call", script, StringComparison.Ordinal);
            Assert.DoesNotContain("MULTI", script, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain("EXEC", script, StringComparison.OrdinalIgnoreCase);
        });
    }

    [Fact]
    public void Hybrid_Authority_Keys_Use_One_Literal_Cluster_Hash_Tag()
    {
        var keys = new ZLinkRedisLocationKeys("zlink-app-a");

        Assert.All(
            new[]
            {
                keys.HybridSchemaKey().ToString(),
                keys.HybridCounterKey().ToString(),
                keys.HybridOwnerLeaseKey("owner").ToString(),
                keys.HybridDescriptorKey("descriptor").ToString(),
                keys.HybridDescriptorAdmissionKey("descriptor").ToString(),
                keys.HybridAuthorityCurrentKey("authority").ToString(),
                keys.HybridAuthorityHistoryKey("authority").ToString(),
                keys.HybridAuthorityHistoryRevisionsKey("authority").ToString(),
                keys.HybridAuthorityKeyIndexKey().ToString(),
                keys.HybridCreationKey(Guid.Empty.ToString("N")).ToString(),
                keys.HybridRelocationKey(Guid.Empty.ToString("N")).ToString(),
                keys.HybridAggregateKey(Guid.Empty, 1).ToString(),
                keys.HybridScanKey(Guid.Empty.ToString("N")).ToString()
            },
            key => Assert.Contains(
                "{zlink-location-v3}",
                key,
                StringComparison.Ordinal));

        Assert.Throws<ArgumentException>(
            () => new ZLinkRedisLocationKeys("zlink:{app-a}"));
    }

    [SkippableFact]
    public async Task Deleted_Change_Stamp_Falls_Back_To_The_Intact_Full_Row_Snapshot()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore(out var prefix);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        var scope = new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play");
        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.True(await store.GetChangeStampAsync(scope) > 0);

        Assert.True(await _fixture.DeleteKeyAsync($"{prefix}:stamp:mesh:play"));
        Assert.Equal(0UL, await store.GetChangeStampAsync(scope));
        Assert.Single(await store.ListMeshNodesAsync("play"));
    }

    [SkippableFact]
    public async Task Dedicated_Run_Prefix_Cleanup_Removes_Every_Derived_Key()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore(out var prefix);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-1"), LeaseTtl);
        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.True(await _fixture.CountPrefixAsync(prefix) > 0);

        Assert.True(await _fixture.DeletePrefixAsync(prefix) > 0);
        Assert.Equal(0, await _fixture.CountPrefixAsync(prefix));
    }

    [SkippableFact]
    public async Task RoutingIdSlotAllocation_IsAtomicIdempotentAndFenced()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone-") };
        var acquired = await Task.WhenAll(Enumerable.Range(1, 20).Select(async owner =>
            Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
                new ZLinkRoutingIdSlotAcquireRequest(
                    "zone",
                    members,
                    20,
                    $"owner-{owner}",
                    LeaseTtl)))));

        Assert.Equal(Enumerable.Range(1, 20), acquired.Select(static item => item.Allocation.Slot).Order());
        Assert.IsType<ZLinkRoutingIdSlotGroupExhausted>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 20, "overflow", LeaseTtl)));

        var first = acquired.Single(static item => item.Allocation.Slot == 1).Allocation;
        var retried = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 20, first.Owner.OwnerId, LeaseTtl)));
        Assert.Equal(first.Owner, retried.Allocation.Owner);
        Assert.Equal(
            ZLinkRoutingIdSlotReleaseResult.Released,
            await store.ReleaseRoutingIdSlotAsync("zone", first.Slot, first.Owner));
        var recycled = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 20, "replacement", LeaseTtl)));
        Assert.Equal(1, recycled.Allocation.Slot);
        Assert.True(recycled.Allocation.Owner.Generation > first.Owner.Generation);
        Assert.Equal(
            ZLinkRoutingIdSlotReleaseResult.IgnoredStale,
            await store.ReleaseRoutingIdSlotAsync("zone", first.Slot, first.Owner));

        var snapshot = await store.ListRoutingIdSlotsAsync("zone");
        Assert.Equal(20, snapshot.Allocations.Count);
        Assert.Equal(members, snapshot.Members);
    }

    [SkippableFact]
    public async Task RoutingIdSlotAllocation_FollowsSharedOwnerLeaseRenewal()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("zone", "zone") };
        var initialTtl = TimeSpan.FromMilliseconds(500);
        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-a"), initialTtl);
        await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerA, initialTtl));

        await store.RenewOwnerLeaseAsync(OwnerA, RoutingId.From("node-a"), TimeSpan.FromSeconds(2));
        await Task.Delay(TimeSpan.FromMilliseconds(600));

        Assert.Contains((await store.ListOwnerLeasesAsync()).Leases,
            static lease => lease.OwnerId == OwnerA && lease.LeaseExpiresAt > lease.UpdatedAt);
        Assert.IsType<ZLinkRoutingIdSlotGroupExhausted>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("zone", members, 1, OwnerB, LeaseTtl)));
        var snapshot = await store.ListRoutingIdSlotsAsync("zone");
        Assert.True(Assert.Single(snapshot.Allocations).LeaseExpiresAt > snapshot.StoreNow);
    }

    [SkippableFact]
    public async Task RoutingIdSlotAllocation_AllowsAllocatedPeersFromAnotherGroup()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        var members = new[] { new ZLinkRoutingIdSlotAllocationMember("play", "play") };

        var play = Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("bingo.play", members, 2, OwnerA, LeaseTtl)));
        await store.UpdateMeshNodeAsync(
            TestRows.MeshNode(OwnerA, nodeRid: $"play{play.Allocation.Slot}"),
            ZLinkLocationWriteIntent.NewClaim);

        Assert.IsType<ZLinkRoutingIdSlotAcquired>(await store.AcquireRoutingIdSlotAsync(
            new ZLinkRoutingIdSlotAcquireRequest("bingo.session", members, 2, OwnerB, LeaseTtl)));
    }

    /// <summary>Creates an isolated store and starts the lease setup; tests
    /// must await the returned task before writing rows.</summary>
    private ZLinkRedisLocationStore CreateStoreWithLiveOwnersAsync(
        string owner, out Task setup, params string[] moreOwners)
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        var store = _fixture.CreateStore();
        setup = RenewAllAsync(store, [owner, .. moreOwners]);
        return store;

        static async Task RenewAllAsync(ZLinkRedisLocationStore store, string[] owners)
        {
            foreach (var ownerId in owners)
            {
                var result = await store.RenewOwnerLeaseAsync(ownerId, RoutingId.From("node-1"), LeaseTtl);
                Assert.True(result.LeaseExpiresAt > result.StoreNow);
            }
        }
    }

    private static async Task<ZLinkLocationWriteResult> WaitForStoredWriteAsync(
        Func<ValueTask<ZLinkLocationWriteResult>> write)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        ZLinkLocationWriteResult result;
        do
        {
            result = await write();
            if (result.Status == ZLinkLocationWriteStatus.Stored)
            {
                return result;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(50));
        } while (DateTimeOffset.UtcNow < deadline);

        return result;
    }

    private static async Task<ZLinkOwnerLeaseSnapshot> WaitForOwnerLeaseToExpireAsync(
        IZLinkOwnerLeaseStore store,
        string expiredOwnerId)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        ZLinkOwnerLeaseSnapshot snapshot;
        do
        {
            snapshot = await store.ListOwnerLeasesAsync();
            if (snapshot.Leases.All(lease => lease.OwnerId != expiredOwnerId))
            {
                return snapshot;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(50));
        } while (DateTimeOffset.UtcNow < deadline);

        return snapshot;
    }
}
