namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisFanoutLocationStoreTests(RedisTestFixture fixture)
{
    [SkippableFact]
    public async Task Publisher_Descriptors_Are_Fenced_And_Paged_By_Channel()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = await ClaimAsync(store, "fanout-owner");
        var first = Descriptor("events", "publisher-a", owner, 7001);
        var second = Descriptor("events", "publisher-b", owner, 7002);
        var other = Descriptor("audit", "publisher-c", owner, 7003);

        var firstWrite = await store.UpdateFanoutPublisherAsync(
            first,
            ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, firstWrite.Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateFanoutPublisherAsync(
                second,
                ZLinkLocationWriteIntent.NewClaim)).Status);
        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            (await store.UpdateFanoutPublisherAsync(
                other,
                ZLinkLocationWriteIntent.NewClaim)).Status);

        var pageOne = await store.ListFanoutPublishersAsync(
            "events",
            new ZLinkPageRequest(PageSize: 1));
        Assert.Equal(first.PublisherRid, Assert.Single(pageOne.Items).PublisherRid);
        Assert.NotNull(pageOne.ContinuationToken);
        var pageTwo = await store.ListFanoutPublishersAsync(
            "events",
            new ZLinkPageRequest(1, pageOne.ContinuationToken));
        Assert.Equal(second.PublisherRid, Assert.Single(pageTwo.Items).PublisherRid);
        Assert.Null(pageTwo.ContinuationToken);

        Assert.Equal(
            ZLinkLocationWriteStatus.IgnoredStale,
            (await store.UpdateFanoutPublisherAsync(
                first with { DescriptorRevision = 1 },
                ZLinkLocationWriteIntent.Renew)).Status);
        var renewed = await store.UpdateFanoutPublisherAsync(
            first with
            {
                DescriptorRevision = 2,
                State = ZLinkFrameworkRuntimeState.Draining
            },
            ZLinkLocationWriteIntent.Renew);
        Assert.Equal(ZLinkLocationWriteStatus.Stored, renewed.Status);
        Assert.Equal(firstWrite.Generation, renewed.Generation);
    }

    [SkippableFact]
    public async Task Remove_And_Owner_Cleanup_Remove_Fanout_Channel_Index()
    {
        Skip.If(!fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var owner = await ClaimAsync(store, "fanout-cleanup");
        var first = Descriptor("events", "publisher-a", owner, 7001);
        var second = Descriptor("events", "publisher-b", owner, 7002);
        await store.UpdateFanoutPublisherAsync(
            first,
            ZLinkLocationWriteIntent.NewClaim);
        await store.UpdateFanoutPublisherAsync(
            second,
            ZLinkLocationWriteIntent.NewClaim);

        Assert.Equal(
            ZLinkLocationWriteStatus.Stored,
            await store.RemoveFanoutPublisherAsync(
                new ZLinkFanoutPublisherDescriptorKey(
                    first.ChannelName,
                    first.PublisherRid),
                owner));
        Assert.Equal(1, await store.RemoveAllByOwnerAsync(owner));
        Assert.Empty(
            (await store.ListFanoutPublishersAsync("events", default)).Items);
    }

    private static async Task<ZLinkLocationOwnerToken> ClaimAsync(
        ZLinkRedisLocationStore store,
        string ownerId)
    {
        var claimed = Assert.IsType<ZLinkOwnerLeaseClaimResult.Claimed>(
            await store.ClaimOwnerLeaseAsync(
                ownerId,
                TimeSpan.FromSeconds(30)));
        return claimed.Token;
    }

    private static ZLinkFanoutPublisherDescriptor Descriptor(
        string channelName,
        string rid,
        ZLinkLocationOwnerToken owner,
        int port) => new(
        channelName,
        RoutingId.From(rid),
        LifecycleGeneration: 1,
        DescriptorRevision: 1,
        Endpoint: $"tcp://127.0.0.1:{port}",
        State: ZLinkFrameworkRuntimeState.Serving,
        SecurityIdentity: "plaintext",
        OwnerId: owner.OwnerId,
        LeaseGeneration: owner.LeaseGeneration,
        UpdatedAt: default);
}
