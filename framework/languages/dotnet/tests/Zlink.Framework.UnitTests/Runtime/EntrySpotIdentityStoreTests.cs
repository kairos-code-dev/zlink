using System.Text.RegularExpressions;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests;

public sealed class EntrySpotIdentityStoreTests
{
    private const string EntrySpotId =
        "play-entry-00000000-0000-4000-8000-000000000001";
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromMinutes(1);

    [Fact]
    public void EntrySpotId_UsesTheDiagnosticPrefixAndLowercaseUuidV4()
    {
        var spotId = ZLinkSpotId.CreateEntrySpotId("play");

        Assert.Matches(
            new Regex(
                "^play-entry-[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$",
                RegexOptions.CultureInvariant),
            spotId);
        var error = Assert.Throws<ZLinkFrameworkException>(() =>
            ZLinkSpotId.RequireCallerProvided(spotId, "spotId"));
        Assert.Equal(
            ZLinkFrameworkErrorKind.InvalidConfiguration,
            error.Kind);
    }

    [Fact]
    public async Task DescriptorClaim_IsGlobalAndBlocksUserAndInstanceSpotClaims()
    {
        var store = new ZLinkInMemoryLocationStore();
        var ownerA = await ClaimOwnerAsync(store, "owner-a", "node-a");
        var ownerB = await ClaimOwnerAsync(store, "owner-b", "node-b");

        var claimed = await store.UpdateMeshNodeAsync(
            Descriptor(ownerA, "node-a", EntrySpotId),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);

        var user = await store.UpdateSpotAsync(
            UserSpot(ownerB, "node-b", EntrySpotId),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, user.Status);

        var instance = await store.ClaimInstanceSpotAsync(
            new InstanceSpotClaimRequest(
                "play",
                EntrySpotId,
                "match",
                RoutingId.From("node-b"),
                1,
                ownerB.OwnerId));
        Assert.IsType<InstanceSpotClaimResult.Conflict>(instance);

        var duplicate = await store.UpdateMeshNodeAsync(
            Descriptor(ownerB, "node-b", EntrySpotId),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(
            ZLinkLocationWriteStatus.RejectedConflict,
            duplicate.Status);
        Assert.Single(await store.ListMeshNodesAsync("play"));

        Assert.IsType<ZLinkObjectReserveResult.Conflict>(
            await store.ReserveAsync(
                UserSpotReservation(ownerB, "node-b")));
    }

    [Fact]
    public async Task ExactDescriptorRemove_ReleasesEntrySpotClaim()
    {
        var store = new ZLinkInMemoryLocationStore();
        var ownerA = await ClaimOwnerAsync(store, "owner-a", "node-a");
        var ownerB = await ClaimOwnerAsync(store, "owner-b", "node-b");
        var descriptor = Descriptor(ownerA, "node-a", EntrySpotId);
        var claimed = await store.UpdateMeshNodeAsync(
            descriptor,
            ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            await store.RemoveMeshNodeAsync(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.MeshName,
                    descriptor.Rid),
                new ZLinkLocationOwnerToken(
                    ownerA.OwnerId,
                    checked((long)claimed.Generation + 1))));
        Assert.Equal(
            ZLinkLocationWriteStatus.RejectedConflict,
            (await store.UpdateSpotAsync(
                UserSpot(ownerB, "node-b", EntrySpotId),
                ZLinkLocationWriteIntent.NewClaim)).Status);

        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            await store.RemoveMeshNodeAsync(
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.MeshName,
                    descriptor.Rid),
                new ZLinkLocationOwnerToken(
                    ownerA.OwnerId,
                    checked((long)claimed.Generation))));

        var user = await store.UpdateSpotAsync(
            UserSpot(ownerB, "node-b", EntrySpotId),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, user.Status);
    }

    private static async ValueTask<ZLinkLocationOwnerToken> ClaimOwnerAsync(
        ZLinkInMemoryLocationStore store,
        string ownerId,
        string nodeRid)
    {
        var claimed = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(ownerId, LeaseTtl));
        await store.RenewOwnerLeaseAsync(
            ownerId,
            RoutingId.From(nodeRid),
            LeaseTtl);
        return claimed.Token;
    }

    private static ZLinkMeshNodeDescriptor Descriptor(
        ZLinkLocationOwnerToken owner,
        string nodeRid,
        string entrySpotId) =>
        new(
            "play",
            RoutingId.From(nodeRid),
            LifecycleGeneration: 1,
            DescriptorRevision: 1,
            $"tcp://127.0.0.1:{(nodeRid == "node-a" ? 7001 : 7002)}",
            new Dictionary<string, int>(StringComparer.Ordinal)
            {
                ["play"] = 100
            },
            SecurityIdentity: string.Empty,
            OwnerId: owner.OwnerId,
            LeaseGeneration: owner.LeaseGeneration,
            UpdatedAt: default)
        {
            ObjectRole = ZLinkMeshNodeObjectRole.Server,
            EntrySpotId = entrySpotId,
            State = ZLinkFrameworkRuntimeState.Serving
        };

    private static ZLinkSpotLocation UserSpot(
        ZLinkLocationOwnerToken owner,
        string nodeRid,
        string spotId) =>
        new(
            "play",
            spotId,
            SpotGeneration: 1,
            RoutingId.From(nodeRid),
            OwnerNodeGeneration: 1,
            ZLinkSpotKind.User,
            SpotType: "match",
            OwnerId: owner.OwnerId,
            UpdatedAt: default);

    private static ZLinkObjectReservationRequest UserSpotReservation(
        ZLinkLocationOwnerToken owner,
        string nodeRid) =>
        new(
            ZLinkPlacementObjectKind.UserSpot,
            ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(EntrySpotId),
            "match",
            "inline-v1:00000000:",
            new byte[32],
            0,
            new ZLinkMeshNodeDescriptorKey(
                "play",
                RoutingId.From(nodeRid)),
            1,
            owner,
            new byte[] { 1 },
            new ZLinkCapacityVector(
                0,
                1,
                new ZLinkSpotTypeCapacityDelta(
                    ZLinkPlacementObjectKind.UserSpot,
                    "match",
                    1)));
}
