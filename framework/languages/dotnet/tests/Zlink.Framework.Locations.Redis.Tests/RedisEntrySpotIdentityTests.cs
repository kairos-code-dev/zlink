namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisEntrySpotIdentityTests(RedisTestFixture fixture)
{
    private const string EntrySpotId =
        "play-entry-00000000-0000-4000-8000-000000000001";

    [SkippableFact]
    public async Task DescriptorClaim_IsAtomicWithGlobalEntryIdentity()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore(out var prefix);
        var ownerA = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "owner-a",
                TimeSpan.FromMinutes(1))).Token;
        var ownerB = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                "owner-b",
                TimeSpan.FromMinutes(1))).Token;

        var first = Descriptor(ownerA, "node-a");
        var claimed = await store.UpdateMeshNodeAsync(
            first,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, claimed.Status);

        var claimKey = new ZLinkRedisLocationKeys(prefix)
            .HybridEntrySpotIdClaimKey(EntrySpotId)
            .ToString();
        var claim = await fixture.HashGetAllAsync(claimKey);
        Assert.Equal(EntrySpotId, claim["spotId"]);
        Assert.Equal(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(
                    first.MeshName,
                    first.Rid)),
            claim["descriptorKey"]);

        var duplicate = await store.UpdateMeshNodeAsync(
            Descriptor(ownerB, "node-b"),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(
            ZLinkLocationWriteStatus.RejectedConflict,
            duplicate.Status);
        Assert.Single(await store.ListMeshNodesAsync("play"));

        var user = await store.UpdateSpotAsync(
            UserSpot(ownerB, "node-b"),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.RejectedConflict, user.Status);
        Assert.IsType<ZLinkObjectReserveResult.Conflict>(
            await store.ReserveAsync(
                UserSpotReservation(ownerB, "node-b")));

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            await store.RemoveMeshNodeAsync(
                new ZLinkMeshNodeDescriptorKey(
                    first.MeshName,
                    first.Rid),
                new ZLinkLocationOwnerToken(
                    ownerA.OwnerId,
                    checked((long)claimed.Generation + 1))));
        Assert.True(await fixture.KeyExistsAsync(claimKey));

        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            await store.RemoveMeshNodeAsync(
                new ZLinkMeshNodeDescriptorKey(
                    first.MeshName,
                    first.Rid),
                new ZLinkLocationOwnerToken(
                    ownerA.OwnerId,
                    checked((long)claimed.Generation))));
        Assert.False(await fixture.KeyExistsAsync(claimKey));

        var afterRemove = await store.UpdateSpotAsync(
            UserSpot(ownerB, "node-b"),
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, afterRemove.Status);
    }

    private static ZLinkMeshNodeDescriptor Descriptor(
        ZLinkLocationOwnerToken owner,
        string nodeRid) =>
        TestRows.MeshNode(
                owner.OwnerId,
                nodeRid: nodeRid,
                leaseGeneration: owner.LeaseGeneration)
            with
            {
                EntrySpotId = EntrySpotId
            };

    private static ZLinkSpotLocation UserSpot(
        ZLinkLocationOwnerToken owner,
        string nodeRid) =>
        TestRows.Spot(owner.OwnerId, EntrySpotId) with
        {
            OwnerNodeRid = RoutingId.From(nodeRid),
            OwnerNodeGeneration = 1
        };

    private static ZLinkObjectReservationRequest UserSpotReservation(
        ZLinkLocationOwnerToken owner,
        string nodeRid) =>
        new(
            ZLinkPlacementObjectKind.UserSpot,
            Zlink.Framework.Runtime.Spots.ZLinkUserSpotAuthorityPayloadCodec
                .AuthorityKey(EntrySpotId),
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
