using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis.Tests;

/// <summary>
/// Failure mapping: read and write APIs report infrastructure failures as
/// exceptions so callers do not have to merge status values with exceptions.
/// These tests point at a closed port and need no Redis.
/// </summary>
public sealed class RedisStoreFailureTests
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
    public async Task Writes_Throw_Infrastructure_Errors()
    {
        await using var store = CreateUnreachableStore();

        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.UpdateActorAsync(TestRows.Actor("owner-a"), ZLinkLocationWriteIntent.NewClaim));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.RemoveActorAsync(
                new ZLinkActorLocationKey("play", "actor-1"),
                new ZLinkLocationOwnerToken("owner-a", 1)));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.RemoveAllByOwnerAsync("owner-a"));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.RenewOwnerLeaseAsync(
                "owner-a", RoutingId.From("node-1"), TimeSpan.FromSeconds(15)));
    }

    [Fact]
    public async Task Reads_Throw_Infrastructure_Errors()
    {
        await using var store = CreateUnreachableStore();

        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.ResolveActorAsync(new ZLinkActorLocationKey("play", "actor-1")));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.ListMeshNodesAsync("play"));
        await Assert.ThrowsAsync<RedisConnectionException>(async () =>
            await store.GetChangeStampAsync(
                new ZLinkLocationChangeStampScope(ZLinkLocationChangeScopeKind.Actor, null)));
    }

    [Fact]
    public void Missing_Key_Prefix_Is_Rejected_At_Construction()
    {
        Assert.Throws<ArgumentException>(() => new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions { ConnectionString = "127.0.0.1:16379" }));
    }
}
