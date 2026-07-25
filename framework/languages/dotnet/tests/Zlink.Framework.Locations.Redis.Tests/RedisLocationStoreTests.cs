using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Spots;

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
        var token = await store.ClaimLiveOwnerAsync(OwnerA, LeaseTtl);

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
                "activationConcurrencyLimit",
                "actorLimit",
                "applicationVersion",
                "capabilities",
                "descriptorKey",
                "descriptorRevision",
                "entrySpotId",
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
        Assert.Contains("for i = 1, 5 do", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.Contains("local ownerIndex = KEYS[i]", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.Contains("redis.call('DEL', ownerIndex)", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.Ordinal);
        Assert.DoesNotContain("MULTI", ZLinkRedisLocationScripts.RemoveAllByOwner, StringComparison.OrdinalIgnoreCase);
    }

    [SkippableFact]
    public async Task Authority_Reserve_Commit_Read_Delete_Roundtrip_Preserves_Exact_Fields()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        var committed = await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        Assert.Equal(1UL, committed.ObjectGeneration);
        Assert.Equal(1UL, committed.AuthorityOwnerGeneration);
        Assert.Equal(OwnerA, committed.OwnerId);
        Assert.Equal(ZLinkPlacementAllocationState.Active, committed.Allocation.State);
        Assert.NotEqual(default, committed.StoreNow);

        Assert.True(ZLinkActorAuthorityPayloadCodec.TryDecode(
            committed.Payload.Span,
            out var payload));
        Assert.Equal("actor-1", payload.ActorId);
        Assert.Equal("player", payload.StableType);
        Assert.Equal(RoutingId.From("node-1"), payload.NodeRid);
        Assert.Equal(ZLinkSpotKind.Entry, payload.CurrentSpotKind);
        Assert.Equal("entry:test", payload.CurrentSpotId);

        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Deleted>(
            await store.CompareExchangeAuthorityAsync(
                ActorAuthorityKey("actor-1"),
                committed.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
        Assert.IsType<ZLinkAuthorityReadResult.Missing>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-1")));
    }

    [SkippableFact]
    public async Task Descriptor_Payloads_Roundtrip_Structured_Fields()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        var descriptors = (await store.ListMeshNodesAsync("play", default)).Items;
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
        Assert.Equal(
            [
                new ZLinkSpotTypeCapacity(
                    ZLinkPlacementObjectKind.UserSpot,
                    "game",
                    0,
                    0,
                    0)
            ],
            descriptor.Capacity.SpotTypes);
        Assert.Equal("wave-a", descriptor.MaintenanceWave);
        Assert.Equal(2, descriptor.ObjectCapabilities.Count);
        var capability = descriptor.ObjectCapabilities.Single(
            static item => item.ObjectKind == ZLinkPlacementObjectKind.Actor);
        Assert.Equal(ZLinkPlacementObjectKind.Actor, capability.ObjectKind);
        Assert.Equal("player", capability.StableType);
        Assert.Equal(0, capability.Limit);
    }

    [SkippableFact]
    public async Task Concurrent_Reserve_From_Two_Owners_Stores_Exactly_One()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;

        for (var attempt = 0; attempt < 10; attempt++)
        {
            var actorId = $"race-{attempt}";
            await EnsureDescriptorAsync(
                store,
                OwnerA,
                ZLinkPlacementObjectKind.Actor);
            await EnsureDescriptorAsync(
                store,
                OwnerB,
                ZLinkPlacementObjectKind.Actor);
            var claims = await Task.WhenAll(
                ReserveAuthorityAsync(
                    store,
                    OwnerA,
                    actorId,
                    ZLinkPlacementObjectKind.Actor).AsTask(),
                ReserveAuthorityAsync(
                    store,
                    OwnerB,
                    actorId,
                    ZLinkPlacementObjectKind.Actor).AsTask());

            Assert.Equal(
                1,
                claims.Count(static result =>
                    result is ZLinkObjectReserveResult.Reserved));
            Assert.Equal(
                1,
                claims.Count(static result =>
                    result is ZLinkObjectReserveResult.Conflict));
        }
    }

    [SkippableFact]
    public async Task Generation_Strictly_Increases_Across_Remove_And_Reclaim()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;

        var first = await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        Assert.Equal(1UL, first.ObjectGeneration);

        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Deleted>(
            await store.CompareExchangeAuthorityAsync(
                ActorAuthorityKey("actor-1"),
                first.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));

        // Generation counters survive row removal, so a re-claim can never
        // reuse an old fencing token.
        var reclaimed = await CreateAuthorityAsync(
            store,
            OwnerB,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        Assert.True(reclaimed.ObjectGeneration > first.ObjectGeneration);
    }

    [SkippableFact]
    public async Task Preserve_Requires_Exact_Version_And_Keeps_Generations()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        var claimed = await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        var stored = Assert.IsType<ZLinkAuthorityCompareExchangeResult.Stored>(
            await store.CompareExchangeAuthorityAsync(
                ActorAuthorityKey("actor-1"),
                claimed.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    claimed.Payload,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    null,
                    null)));
        Assert.Equal(claimed.ObjectGeneration, stored.Snapshot.ObjectGeneration);
        Assert.Equal(
            claimed.AuthorityOwnerGeneration,
            stored.Snapshot.AuthorityOwnerGeneration);

        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Conflict>(
            await store.CompareExchangeAuthorityAsync(
                ActorAuthorityKey("actor-1"),
                claimed.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
    }

    [SkippableFact]
    public async Task Owner_Lease_Expiry_Does_Not_Silently_Replace_Authority()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        await store.ClaimLiveOwnerAsync(OwnerA, TimeSpan.FromMilliseconds(300));
        await store.ClaimLiveOwnerAsync(OwnerB, LeaseTtl);

        var first = await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        await Task.Delay(TimeSpan.FromMilliseconds(500));

        Assert.IsType<ZLinkObjectReserveResult.AlreadyExists>(
            await ReserveAuthorityAsync(
                store,
                OwnerB,
                "actor-1",
                ZLinkPlacementObjectKind.Actor));
        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Conflict>(
            await store.CompareExchangeAuthorityAsync(
                ActorAuthorityKey("actor-1"),
                first.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
    }

    [SkippableFact]
    public async Task RemoveByOwner_RemovesOnlyEphemeralRowsForExactOwner()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup, OwnerB);
        await setup;
        await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-1",
            ZLinkPlacementObjectKind.Actor);
        await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-2",
            ZLinkPlacementObjectKind.Actor);
        await CreateAuthorityAsync(
            store,
            OwnerB,
            "actor-3",
            ZLinkPlacementObjectKind.Actor);

        var ownerAToken = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA)).Token;
        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(1, removed);
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-1")));
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-2")));
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-3")));
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
        await CreateAuthorityAsync(
            store,
            OwnerA,
            "spot-a",
            ZLinkPlacementObjectKind.UserSpot);
        await CreateAuthorityAsync(
            store,
            OwnerB,
            "spot-b",
            ZLinkPlacementObjectKind.UserSpot);
        await CreateAuthorityAsync(
            store,
            OwnerA,
            "actor-a",
            ZLinkPlacementObjectKind.Actor);
        await CreateAuthorityAsync(
            store,
            OwnerB,
            "actor-b",
            ZLinkPlacementObjectKind.Actor);

        var removed = await store.RemoveAllByOwnerAsync(ownerAToken);

        Assert.Equal(2, removed);
        Assert.DoesNotContain((await store.ListMeshNodesAsync("play", default)).Items, row => row.OwnerId == OwnerA);
        Assert.Contains((await store.ListMeshNodesAsync("play", default)).Items, row => row.OwnerId == OwnerB);
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(SpotAuthorityKey("spot-a")));
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(SpotAuthorityKey("spot-b")));
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-a")));
        Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(ActorAuthorityKey("actor-b")));
    }

    [SkippableFact]
    public async Task Spot_Authority_Roundtrip_Preserves_Payload_And_Fences_Delete()
    {
        await using var store = CreateStoreWithLiveOwnersAsync(OwnerA, out var setup);
        await setup;

        var claimed = await CreateAuthorityAsync(
            store,
            OwnerA,
            "spot-1",
            ZLinkPlacementObjectKind.UserSpot);
        Assert.True(ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
            claimed.Payload.Span,
            out var decoded));
        Assert.Equal("spot-1", decoded.SpotId);
        Assert.Equal("game", decoded.StableType);
        Assert.Equal(RoutingId.From("node-1"), decoded.NodeRid);
        Assert.Equal(OwnerA, decoded.OwnerId);

        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Conflict>(
            await store.CompareExchangeAuthorityAsync(
                SpotAuthorityKey("spot-1"),
                claimed.StoreVersion + "-stale",
                new ZLinkAuthorityMutation.Delete()));
        Assert.IsType<ZLinkAuthorityCompareExchangeResult.Deleted>(
            await store.CompareExchangeAuthorityAsync(
                SpotAuthorityKey("spot-1"),
                claimed.StoreVersion,
                new ZLinkAuthorityMutation.Delete()));
        Assert.IsType<ZLinkAuthorityReadResult.Missing>(
            await store.ReadAuthorityAsync(SpotAuthorityKey("spot-1")));
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
        var owner = await store.ReadOwnerLeaseAsync(OwnerA);
        var current = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(owner).Token;
        var stale = await store.RemoveMeshNodeAsync(
            key,
            new ZLinkLocationOwnerToken(
                OwnerA,
                checked(current.LeaseGeneration + 1)));
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, stale);
        Assert.Single((await store.ListMeshNodesAsync("play", default)).Items);

        var removed = await store.RemoveMeshNodeAsync(key, current);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, removed);
        Assert.Empty((await store.ListMeshNodesAsync("play", default)).Items);
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

        await store.ListMeshNodesAsync("play", default);
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
    public async Task Owner_Lease_Read_Reports_StoreNow_And_Omits_Expired_Lease()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore();
        var ownerA = await store.ClaimLiveOwnerAsync(OwnerA, LeaseTtl);
        await store.ClaimLiveOwnerAsync(OwnerB, TimeSpan.FromMilliseconds(300));

        await WaitForOwnerLeaseToExpireAsync(store, OwnerB);
        var lease = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(OwnerA));
        Assert.Equal(ownerA, lease.Token);
        Assert.NotEqual(default, lease.StoreNow);
        // Expiry is derived from StoreNow plus the remaining Redis TTL; the
        // application wall clock never participates in the comparison.
        Assert.True(lease.LeaseExpiresAt > lease.StoreNow);
        Assert.True(lease.LeaseExpiresAt - lease.StoreNow <= LeaseTtl);

        Assert.Equal(
            ZLinkOwnerLeaseReleaseResult.Released,
            await store.ReleaseOwnerLeaseAsync(ownerA));
        Assert.IsType<ZLinkOwnerLeaseReadResult.Missing>(
            await store.ReadOwnerLeaseAsync(OwnerA));
    }

    [Fact]
    public void Every_Write_Operation_Has_One_Atomic_Lua_Script()
    {
        var scripts = new[]
        {
            ZLinkRedisLocationScripts.Write,
            ZLinkRedisLocationScripts.Remove,
            ZLinkRedisLocationScripts.RemoveAllByOwner,
            ZLinkRedisLocationScripts.ClaimLease,
            ZLinkRedisLocationScripts.RenewExactLease,
            ZLinkRedisLocationScripts.ReleaseExactLease
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
        await store.ClaimLiveOwnerAsync(OwnerA, LeaseTtl);
        var scope = new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.MeshNode, "play");
        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.True(await store.GetChangeStampAsync(scope) > 0);

        Assert.True(await _fixture.DeleteKeyAsync($"{prefix}:stamp:mesh:play"));
        Assert.Equal(0UL, await store.GetChangeStampAsync(scope));
        Assert.Single((await store.ListMeshNodesAsync("play", default)).Items);
    }

    [SkippableFact]
    public async Task Dedicated_Run_Prefix_Cleanup_Removes_Every_Derived_Key()
    {
        Skip.If(!_fixture.RedisAvailable, _fixture.SkipReason);
        await using var store = _fixture.CreateStore(out var prefix);
        await store.ClaimLiveOwnerAsync(OwnerA, LeaseTtl);
        await store.UpdateMeshNodeAsync(TestRows.MeshNode(OwnerA), ZLinkLocationWriteIntent.NewClaim);
        Assert.True(await _fixture.CountPrefixAsync(prefix) > 0);

        Assert.True(await _fixture.DeletePrefixAsync(prefix) > 0);
        Assert.Equal(0, await _fixture.CountPrefixAsync(prefix));
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
                var token = await store.ClaimLiveOwnerAsync(ownerId, LeaseTtl);
                var read = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
                    await store.ReadOwnerLeaseAsync(ownerId));
                Assert.Equal(token, read.Token);
                Assert.True(read.LeaseExpiresAt > read.StoreNow);
            }
        }
    }

    private static ZLinkAuthorityKey ActorAuthorityKey(string actorId) =>
        ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);

    private static ZLinkAuthorityKey SpotAuthorityKey(string spotId) =>
        ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(spotId);

    private static async ValueTask EnsureDescriptorAsync(
        ZLinkRedisLocationStore store,
        string ownerId,
        ZLinkPlacementObjectKind objectKind)
    {
        var owner = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(ownerId)).Token;
        var nodeName = ownerId == OwnerB ? "node-2" : "node-1";
        var nodeRid = RoutingId.From(nodeName);
        if ((await store.ListMeshNodesAsync("play", default)).Items.Any(
                item => item.Rid == nodeRid))
            return;

        var descriptor = TestRows.MeshNode(
            ownerId,
            nodeRid: nodeName,
            leaseGeneration: owner.LeaseGeneration);
        var written = await store.UpdateMeshNodeAsync(
            descriptor,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, written.Status);
        Assert.Contains(
            descriptor.ObjectCapabilities,
            item => item.ObjectKind == objectKind);
    }

    private static async ValueTask<ZLinkObjectReserveResult>
        ReserveAuthorityAsync(
            ZLinkRedisLocationStore store,
            string ownerId,
            string objectId,
            ZLinkPlacementObjectKind objectKind)
    {
        await EnsureDescriptorAsync(store, ownerId, objectKind);
        var owner = Assert.IsType<ZLinkOwnerLeaseReadResult.Found>(
            await store.ReadOwnerLeaseAsync(ownerId)).Token;
        var nodeName = ownerId == OwnerB ? "node-2" : "node-1";
        var nodeRid = RoutingId.From(nodeName);
        var key = objectKind == ZLinkPlacementObjectKind.Actor
            ? ActorAuthorityKey(objectId)
            : SpotAuthorityKey(objectId);
        var stableType = objectKind == ZLinkPlacementObjectKind.Actor
            ? "player"
            : "game";
        var intent = System.Text.Encoding.UTF8.GetBytes(
            $"create:{key.Value}:{ownerId}");
        var creatingPayload = AuthorityPayload(
            owner,
            nodeRid,
            objectId,
            objectKind,
            ready: false);
        return await store.ReserveAsync(
            new ZLinkObjectReservationRequest(
                objectKind,
                key,
                stableType,
                $"inline:{key.Value}",
                System.Security.Cryptography.SHA256.HashData(intent),
                intent.Length,
                new ZLinkMeshNodeDescriptorKey("play", nodeRid),
                1,
                owner,
                creatingPayload,
                objectKind == ZLinkPlacementObjectKind.Actor
                    ? new ZLinkCapacityVector(1, 0, null)
                    : new ZLinkCapacityVector(
                        0,
                        1,
                        new ZLinkSpotTypeCapacityDelta(
                            ZLinkPlacementObjectKind.UserSpot,
                            stableType,
                            1))));
    }

    private static async ValueTask<ZLinkAuthoritySnapshot>
        CreateAuthorityAsync(
            ZLinkRedisLocationStore store,
            string ownerId,
            string objectId,
            ZLinkPlacementObjectKind objectKind)
    {
        var reserved = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await ReserveAuthorityAsync(
                store,
                ownerId,
                objectId,
                objectKind));
        var nodeRid = RoutingId.From(ownerId == OwnerB ? "node-2" : "node-1");
        var payload = AuthorityPayload(
            reserved.Reservation.TargetOwner,
            nodeRid,
            objectId,
            objectKind,
            ready: true);
        return Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                reserved.Reservation,
                payload)).Snapshot;
    }

    private static byte[] AuthorityPayload(
        ZLinkLocationOwnerToken owner,
        RoutingId nodeRid,
        string objectId,
        ZLinkPlacementObjectKind objectKind,
        bool ready)
    {
        if (objectKind == ZLinkPlacementObjectKind.Actor)
        {
            return ZLinkActorAuthorityPayloadCodec.Encode(
                new ZLinkActorAuthorityPayload(
                    ready
                        ? ZLinkActorAuthorityState.Ready
                        : ZLinkActorAuthorityState.Creating,
                    "player",
                    objectId,
                    "entry:test",
                    1,
                    ZLinkSpotKind.Entry,
                    owner.OwnerId,
                    checked((ulong)owner.LeaseGeneration),
                    "play",
                    nodeRid,
                    1));
        }

        return ZLinkUserSpotAuthorityPayloadCodec.Encode(
            new ZLinkUserSpotAuthorityPayload(
                ready
                    ? ZLinkUserSpotAuthorityState.Ready
                    : ZLinkUserSpotAuthorityState.Creating,
                "game",
                objectId,
                owner.OwnerId,
                checked((ulong)owner.LeaseGeneration),
                "play",
                nodeRid,
                1));
    }

    private static async Task WaitForOwnerLeaseToExpireAsync(
        ZLinkRedisLocationStore store,
        string expiredOwnerId)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        do
        {
            if (await store.ReadOwnerLeaseAsync(expiredOwnerId)
                is ZLinkOwnerLeaseReadResult.Missing)
                return;

            await Task.Delay(TimeSpan.FromMilliseconds(50));
        } while (DateTimeOffset.UtcNow < deadline);

        Assert.IsType<ZLinkOwnerLeaseReadResult.Missing>(
            await store.ReadOwnerLeaseAsync(expiredOwnerId));
    }
}
