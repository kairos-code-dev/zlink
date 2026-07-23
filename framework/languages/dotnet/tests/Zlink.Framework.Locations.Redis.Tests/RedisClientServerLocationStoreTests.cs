namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisClientServerLocationStoreTests(
    RedisTestFixture fixture)
{
    private static readonly TimeSpan LeaseTtl = TimeSpan.FromSeconds(30);

    [SkippableFact]
    public async Task Server_Descriptors_Are_Fenced_And_Paged_By_Channel()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = await ClaimAsync(store, "clientserver-owner");

        var first = Descriptor(
            "orders",
            "server-a",
            owner,
            endpoint: "tcp://127.0.0.1:5001");
        var second = Descriptor(
            "orders",
            "server-b",
            owner,
            endpoint: "tcp://127.0.0.1:5002");
        var otherChannel = Descriptor(
            "billing",
            "server-c",
            owner,
            endpoint: "tcp://127.0.0.1:5003");

        var firstWrite = await store.UpdateClientServerAsync(
            first,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, firstWrite.Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateClientServerAsync(
                second,
                ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateClientServerAsync(
                otherChannel,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        var pageOne = await store.ListClientServersAsync(
            "orders",
            new ZLinkPageRequest(PageSize: 1));
        var pageOneItem = Assert.Single(pageOne.Items);
        Assert.Equal(first.ServerRid, pageOneItem.ServerRid);
        Assert.NotEqual(default, pageOneItem.UpdatedAt);
        Assert.NotNull(pageOne.ContinuationToken);

        var pageTwo = await store.ListClientServersAsync(
            "orders",
            new ZLinkPageRequest(
                PageSize: 1,
                ContinuationToken: pageOne.ContinuationToken));
        Assert.Equal(second.ServerRid, Assert.Single(pageTwo.Items).ServerRid);
        Assert.Null(pageTwo.ContinuationToken);

        var staleRevision = await store.UpdateClientServerAsync(
            first with { DescriptorRevision = first.DescriptorRevision },
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.IgnoredStale, staleRevision.Status);

        var immutableChange = await store.UpdateClientServerAsync(
            first with
            {
                DescriptorRevision = first.DescriptorRevision + 1,
                Endpoint = "tcp://127.0.0.1:5999"
            },
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            immutableChange.Status);

        var renewed = await store.UpdateClientServerAsync(
            first with
            {
                DescriptorRevision = first.DescriptorRevision + 1,
                Weight = 0,
                State = ZLinkFrameworkRuntimeState.Draining
            },
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(firstWrite.Generation, renewed.Generation);

        var updated = Assert.Single(
            (await store.ListClientServersAsync("orders", default)).Items,
            row => row.ServerRid == first.ServerRid);
        Assert.Equal(0, updated.Weight);
        Assert.Equal(ZLinkFrameworkRuntimeState.Draining, updated.State);
    }

    [SkippableFact]
    public async Task Remove_And_Owner_Cleanup_Update_Channel_Index_Atomically()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = await ClaimAsync(store, "cleanup-owner");
        var first = Descriptor("orders", "server-a", owner);
        var second = Descriptor("orders", "server-b", owner);

        await store.UpdateClientServerAsync(
            first,
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateClientServerAsync(
            second,
            ZLinkLocationWriteIntent.NewClaim);

        var staleOwner = owner with
        {
            LeaseGeneration = owner.LeaseGeneration + 1
        };
        Assert.Equal(0, await store.RemoveAllByOwnerAsync(staleOwner));
        Assert.Equal(
            2,
            (await store.ListClientServersAsync("orders", default)).Items.Count);

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            await store.RemoveClientServerAsync(
                new ZLinkClientServerServerDescriptorKey(
                    first.ChannelName,
                    first.ServerRid),
                staleOwner));
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            await store.RemoveClientServerAsync(
                new ZLinkClientServerServerDescriptorKey(
                    first.ChannelName,
                    first.ServerRid),
                owner));

        Assert.Equal(1, await store.RemoveAllByOwnerAsync(owner));
        Assert.Empty(
            (await store.ListClientServersAsync("orders", default)).Items);
    }

    [SkippableFact]
    public async Task Takeover_Requires_Expired_Current_Owner_Lease_And_Fences_Old_Token()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var originalOwner = await ClaimAsync(
            store,
            "takeover-original",
            TimeSpan.FromMilliseconds(250));
        var nextOwner = await ClaimAsync(store, "takeover-next");
        var original = Descriptor(
            "orders",
            "server-a",
            originalOwner,
            endpoint: "tcp://127.0.0.1:5001");
        var originalWrite = await store.UpdateClientServerAsync(
            original,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, originalWrite.Status);

        var replacement = original with
        {
            LifecycleGeneration = 2,
            DescriptorRevision = 1,
            Endpoint = "tcp://127.0.0.1:5002",
            OwnerId = nextOwner.OwnerId,
            LeaseGeneration = nextOwner.LeaseGeneration
        };
        Assert.Equal(
            ZLinkLocationWriteStatus.RejectedConflict,
            (await store.UpdateClientServerAsync(
                replacement,
                ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateClientServerAsync(
                replacement,
                ZLinkLocationWriteIntent.Takeover)).Status);

        await Task.Delay(TimeSpan.FromMilliseconds(350));

        var takeover = await store.UpdateClientServerAsync(
            replacement,
            ZLinkLocationWriteIntent.Takeover);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, takeover.Status);
        Assert.NotEqual(originalWrite.Generation, takeover.Generation);

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            await store.RemoveClientServerAsync(
                new ZLinkClientServerServerDescriptorKey(
                    original.ChannelName,
                    original.ServerRid),
                originalOwner));

        var listed = Assert.Single(
            (await store.ListClientServersAsync("orders", default)).Items);
        Assert.Equal(2UL, listed.LifecycleGeneration);
        Assert.Equal(nextOwner.OwnerId, listed.OwnerId);
        Assert.Equal(nextOwner.LeaseGeneration, listed.LeaseGeneration);
        Assert.Equal("tcp://127.0.0.1:5002", listed.Endpoint);
    }

    private static async Task<ZLinkLocationOwnerToken> ClaimAsync(
        ZLinkRedisLocationStore store,
        string ownerId,
        TimeSpan? leaseTtl = null)
    {
        var claimed = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                ownerId,
                leaseTtl ?? LeaseTtl));
        return claimed.Token;
    }

    private static ZLinkClientServerServerDescriptor Descriptor(
        string channelName,
        string rid,
        ZLinkLocationOwnerToken owner,
        string endpoint = "tcp://127.0.0.1:5001") => new(
        channelName,
        RoutingId.From(rid),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        endpoint,
        Weight: 100,
        State: ZLinkFrameworkRuntimeState.Serving,
        SecurityIdentity: "plaintext",
        OwnerId: owner.OwnerId,
        LeaseGeneration: owner.LeaseGeneration,
        UpdatedAt: default);
}
