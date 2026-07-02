using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>
/// Failure mapping (draft 7.6): write APIs report an unreachable store as
/// <see cref="ZLinkLocationWriteStatus.StoreUnavailable"/>, read APIs throw
/// an infrastructure exception so the caller can distinguish "store down"
/// from "not found". These tests point at a closed port and need no Redis.
/// </summary>
public sealed class RedisStoreUnavailableTests
{
    private static ZLinkRedisLocationStore CreateUnreachableStore() =>
        new(new ZLinkRedisLocationOptions
        {
            // Nothing listens on this port; keep the connect timeout short so
            // the failure surfaces quickly.
            ConnectionString = "127.0.0.1:59999,connectTimeout=500,connectRetry=0",
            KeyPrefix = "zlink:test:unreachable"
        });

    [Fact]
    public async Task Writes_Report_StoreUnavailable_Instead_Of_Throwing()
    {
        await using var store = CreateUnreachableStore();

        var update = await store.UpdateActorAsync(
            TestRows.Actor("owner-a", 0), ZLinkLocationWriteIntent.NewClaim);
        Assert.Equal(ZLinkLocationWriteStatus.StoreUnavailable, update.Status);

        var remove = await store.RemoveActorAsync(
            new ZLinkActorLocationKey("player", "actor-1"),
            new ZLinkLocationOwnerToken("owner-a", 1));
        Assert.Equal(ZLinkLocationWriteStatus.StoreUnavailable, remove.Status);

        var lease = await store.RenewOwnerLeaseAsync(
            "owner-a", RoutingId.From("node-1"), TimeSpan.FromSeconds(15));
        Assert.Equal(ZLinkLocationWriteStatus.StoreUnavailable, lease.Status);
    }

    [Fact]
    public async Task Reads_Throw_Infrastructure_Errors()
    {
        await using var store = CreateUnreachableStore();

        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.ResolveActorAsync(new ZLinkActorLocationKey("player", "actor-1")));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.ListPeersAsync(new ZLinkPeerLocationFilter()));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.GetChangeStampAsync(
                new ZLinkLocationChangeStampScope(ZLinkLocationKind.Actor, null)));
    }

    [Fact]
    public void Missing_Key_Prefix_Is_Rejected_At_Construction()
    {
        Assert.Throws<ArgumentException>(() => new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions { ConnectionString = "127.0.0.1:16379" }));
    }
}
