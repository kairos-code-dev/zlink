using System.Text;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisCrossLanguageTests
{
    private static readonly ZLinkAuthorityKey NodeActorKey =
        new("cross:actor:node-actor");
    private static readonly ZLinkAuthorityKey NodeSpotKey =
        new("cross:spot:node-spot");
    private static readonly ZLinkAuthorityKey DotnetActorKey =
        new("cross:actor:dotnet-actor");
    private static readonly ZLinkAuthorityKey DotnetSpotKey =
        new("cross:spot:dotnet-spot");

    [SkippableFact]
    public async Task Dotnet_Reads_Node_Authorities()
    {
        SkipUnlessHarnessConfigured();
        await using var store = CreateStore("node");

        var actor = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(NodeActorKey)).Snapshot;
        Assert.Equal("node-owner", actor.OwnerId);
        Assert.Equal(1UL, actor.ObjectGeneration);
        Assert.Equal(
            "node-actor-ready-v1",
            Encoding.UTF8.GetString(actor.Payload.Span));

        var spot = Assert.IsType<ZLinkAuthorityReadResult.Found>(
            await store.ReadAuthorityAsync(NodeSpotKey)).Snapshot;
        Assert.Equal("node-owner", spot.OwnerId);
        Assert.Equal(ZLinkPlacementObjectKind.UserSpot, spot.Allocation.ObjectKind);
        Assert.Equal(
            "node-spot-ready-v1",
            Encoding.UTF8.GetString(spot.Payload.Span));

        var descriptors = (await store.ListMeshNodesAsync("cross", default)).Items;
        var descriptor = Assert.Single(
            descriptors,
            row => row.Rid.Equals(RoutingId.From("node-node")));
        Assert.Equal("tcp://127.0.0.1:5320", descriptor.Endpoint);
        Assert.Equal(ZLinkFrameworkRuntimeState.Draining, descriptor.State);
        Assert.Equal(100, descriptor.ChannelWeights["cross"]);
    }

    [SkippableFact]
    public async Task Dotnet_Writes_Authorities_For_Node_To_Read()
    {
        SkipUnlessHarnessConfigured();
        const string ownerId = "dotnet-owner";
        var nodeRid = RoutingId.From("dotnet-node");
        await using var store = CreateStore("dotnet");
        var owner = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                ownerId,
                TimeSpan.FromSeconds(30))).Token;
        await store.RenewOwnerLeaseAsync(owner, TimeSpan.FromSeconds(30));

        var descriptor = CreateDescriptor(owner, nodeRid);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateMeshNodeAsync(
                descriptor,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        var actor = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await ReserveAsync(
                store,
                owner,
                nodeRid,
                DotnetActorKey,
                ZLinkPlacementObjectKind.Actor,
                "player",
                new ZLinkCapacityVector(1, 0, null)));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                actor.Reservation,
                Encoding.UTF8.GetBytes("dotnet-actor-ready-v1")));

        var spot = Assert.IsType<ZLinkObjectReserveResult.Reserved>(
            await ReserveAsync(
                store,
                owner,
                nodeRid,
                DotnetSpotKey,
                ZLinkPlacementObjectKind.UserSpot,
                "dotnet-game",
                new ZLinkCapacityVector(
                    0,
                    1,
                    new ZLinkSpotTypeCapacityDelta(
                        ZLinkPlacementObjectKind.UserSpot,
                        "dotnet-game",
                        1))));
        Assert.IsType<ZLinkObjectCommitResult.Committed>(
            await store.CommitAsync(
                spot.Reservation,
                Encoding.UTF8.GetBytes("dotnet-spot-ready-v1")));
    }

    private static ZLinkMeshNodeDescriptor CreateDescriptor(
        ZLinkLocationOwnerToken owner,
        RoutingId nodeRid) =>
        new(
            "cross",
            nodeRid,
            LifecycleGeneration: 1,
            DescriptorRevision: 1,
            "tcp://127.0.0.1:5310",
            new Dictionary<string, int>(StringComparer.Ordinal)
            {
                ["cross"] = 100
            },
            SecurityIdentity: "cluster-a",
            OwnerId: owner.OwnerId,
            LeaseGeneration: owner.LeaseGeneration,
            UpdatedAt: default)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            ObjectCapabilities =
            [
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.Actor,
                    "player",
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    HasSnapshotAdapter: false,
                    Limit: 0),
                new ZLinkObjectCapability(
                    ZLinkPlacementObjectKind.UserSpot,
                    "dotnet-game",
                    ZLinkObjectMaintenancePolicyKind.Recreate,
                    HasSnapshotAdapter: false,
                    Limit: 1_000)
            ],
            Capacity = new(
                new ZLinkPopulationCapacity(1_000, 0, 1_000),
                new ZLinkPopulationCapacity(1_000, 0, 1_000),
                [
                    new ZLinkSpotTypeCapacity(
                        ZLinkPlacementObjectKind.UserSpot,
                        "dotnet-game",
                        1_000,
                        0,
                        1_000)
                ]),
            EntrySpotId =
                "cross-entry-00000000-0000-4000-8000-000000000001",
            State = ZLinkFrameworkRuntimeState.Draining
        };

    private static ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        IZLinkLocationStore store,
        ZLinkLocationOwnerToken owner,
        RoutingId nodeRid,
        ZLinkAuthorityKey key,
        ZLinkPlacementObjectKind kind,
        string stableType,
        ZLinkCapacityVector capacity)
    {
        var intent = Encoding.UTF8.GetBytes($"create:{key.Value}");
        return store.ReserveAsync(
            new ZLinkObjectReservationRequest(
                kind,
                key,
                stableType,
                $"inline:{key.Value}",
                System.Security.Cryptography.SHA256.HashData(intent),
                intent.Length,
                new ZLinkMeshNodeDescriptorKey("cross", nodeRid),
                1,
                owner,
                Encoding.UTF8.GetBytes($"creating:{key.Value}"),
                capacity));
    }

    private static void SkipUnlessHarnessConfigured()
    {
        Skip.If(
            Environment.GetEnvironmentVariable("ZLINK_REDIS_TEST_ENDPOINT") is null
            || Environment.GetEnvironmentVariable(
                "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX") is null,
            "Cross-language Redis harness environment is not configured.");
    }

    private static ZLinkRedisLocationStore CreateStore(string suffix)
    {
        var endpoint = Environment.GetEnvironmentVariable("ZLINK_REDIS_TEST_ENDPOINT")
            ?? throw new InvalidOperationException(
                "ZLINK_REDIS_TEST_ENDPOINT is required.");
        var prefix = Environment.GetEnvironmentVariable("ZLINK_REDIS_CROSS_LANGUAGE_PREFIX")
            ?? throw new InvalidOperationException(
                "ZLINK_REDIS_CROSS_LANGUAGE_PREFIX is required.");
        return new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions
        {
            ConnectionString = endpoint,
            KeyPrefix = $"{prefix}:{suffix}"
        });
    }
}
