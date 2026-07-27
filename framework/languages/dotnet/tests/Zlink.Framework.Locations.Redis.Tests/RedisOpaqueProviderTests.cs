namespace Zlink.Framework.Locations.Redis.Tests;

[Collection(RedisTestCollection.Name)]
public sealed class RedisOpaqueProviderTests(RedisTestFixture fixture)
{
    [Fact]
    public void Location_provider_implements_only_the_opaque_store_contract()
    {
        var interfaces = typeof(ZLinkRedisLocationStore).GetInterfaces();

        Assert.Contains(typeof(IZLinkLocationStore), interfaces);
        Assert.DoesNotContain(
            interfaces,
            type => type.Name == "IZLinkLocationRepository");
        Assert.DoesNotContain(
            typeof(ZLinkRedisLocationStore).Assembly.GetTypes(),
            type => type.Name.StartsWith(
                        "ZLinkRedisAuthority",
                        StringComparison.Ordinal)
                    || type.Name is "ZLinkRedisLocationCommands"
                        or "ZLinkRedisLocationKeyCodec"
                        or "ZLinkRedisLocationRowJson");
    }

    [SkippableFact]
    public async Task Location_store_applies_the_conditional_batch_atomically()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var key = new ZLinkStoreKey("authority:actor:player-1");

        var first = Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Missing(key)],
                [new ZLinkStoreMutation.Put(key, new byte[] { 1, 2, 3 }, null)])));
        var version = Assert.Single(first.PutVersions).Value;

        Assert.IsType<ZLinkStoreWriteResult.Conflict>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Missing(key)],
                [new ZLinkStoreMutation.Delete(key)])));

        Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Version(key, version)],
                [new ZLinkStoreMutation.Put(key, new byte[] { 4 }, null)])));
        var found = Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(key));
        Assert.Equal(new byte[] { 4 }, found.Value.Bytes.ToArray());
    }

    [SkippableFact]
    public async Task Relocation_store_preserves_caller_reference_idempotency()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateRelocationStore();
        var reference = new ZLinkBlobReference(Guid.NewGuid().ToString("N"));
        var retention = TimeSpan.FromMinutes(1);

        Assert.IsType<ZLinkBlobPutResult.Stored>(
            await store.PutAsync(reference, new byte[] { 1, 2 }, retention));
        Assert.IsType<ZLinkBlobPutResult.AlreadyStored>(
            await store.PutAsync(reference, new byte[] { 1, 2 }, retention));
        Assert.IsType<ZLinkBlobPutResult.Conflict>(
            await store.PutAsync(reference, new byte[] { 9 }, retention));

        var found = Assert.IsType<ZLinkBlobReadResult.Found>(
            await store.ReadAsync(reference));
        Assert.Equal(new byte[] { 1, 2 }, found.Bytes.ToArray());
        Assert.IsType<ZLinkBlobRenewResult.Renewed>(
            await store.RenewAsync(reference, retention));

        await store.DeleteAsync(reference);
        Assert.IsType<ZLinkBlobReadResult.Missing>(
            await store.ReadAsync(reference));
        Assert.IsType<ZLinkBlobRenewResult.Missing>(
            await store.RenewAsync(reference, retention));
    }

    [SkippableFact]
    public async Task Location_scan_keeps_the_first_page_snapshot()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        foreach (var (key, value) in new[]
                 {
                     ("actor:1", (byte)1),
                     ("actor:2", (byte)2),
                     ("actor:3", (byte)3),
                     ("other:1", (byte)9)
                 })
        {
            Assert.IsType<ZLinkStoreWriteResult.Applied>(
                await store.WriteAsync(new ZLinkStoreWriteRequest(
                    [new ZLinkStoreCondition.Missing(new ZLinkStoreKey(key))],
                    [new ZLinkStoreMutation.Put(
                        new ZLinkStoreKey(key),
                        new[] { value },
                        null)])));
        }

        var first = Assert.IsType<ZLinkStoreScanResult.Page>(
            await store.ScanAsync(new ZLinkStoreScanRequest(
                "actor:",
                null,
                2)));
        Assert.Equal(2, first.Value.Items.Count);
        Assert.NotNull(first.Value.NextCursor);

        var thirdKey = new ZLinkStoreKey("actor:3");
        var third = Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(thirdKey));
        Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Version(thirdKey, third.Value.Version)],
                [new ZLinkStoreMutation.Put(thirdKey, new byte[] { 8 }, null)])));

        var second = Assert.IsType<ZLinkStoreScanResult.Page>(
            await store.ScanAsync(new ZLinkStoreScanRequest(
                "actor:",
                first.Value.NextCursor,
                2)));
        var item = Assert.Single(second.Value.Items);
        Assert.Equal("actor:3", item.Key.Value);
        Assert.Equal(new byte[] { 3 }, item.Value.Bytes.ToArray());
        Assert.Null(second.Value.NextCursor);

        Assert.IsType<ZLinkStoreScanResult.Expired>(
            await store.ScanAsync(new ZLinkStoreScanRequest(
                "actor:",
                new ZLinkStoreScanCursor(
                    $"{Guid.NewGuid():N}:0"),
                2)));
    }

    [SkippableFact]
    public async Task Conflict_does_not_apply_any_mutation_or_issue_a_version()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var guard = new ZLinkStoreKey("batch:guard");
        var target = new ZLinkStoreKey("batch:target");
        var initial = Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Missing(guard)],
                [new ZLinkStoreMutation.Put(guard, new byte[] { 1 }, null)])));

        var conflict = Assert.IsType<ZLinkStoreWriteResult.Conflict>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [new ZLinkStoreCondition.Missing(guard)],
                [new ZLinkStoreMutation.Put(target, new byte[] { 2 }, null)])));

        Assert.True(conflict.StoreNow >= initial.StoreNow);
        Assert.IsType<ZLinkStoreReadResult.Missing>(
            await store.ReadAsync(target));
        var unchanged = Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(guard));
        Assert.Equal(
            initial.PutVersions[guard],
            unchanged.Value.Version);
    }

    [SkippableFact]
    public async Task Expiring_and_durable_values_follow_provider_time()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var expiring = new ZLinkStoreKey("retention:short");
        var durable = new ZLinkStoreKey("retention:durable");
        var applied = Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest(
                [],
                [
                    new ZLinkStoreMutation.Put(
                        expiring,
                        new byte[] { 1 },
                        TimeSpan.FromMilliseconds(150)),
                    new ZLinkStoreMutation.Put(
                        durable,
                        new byte[] { 2 },
                        null)
                ])));
        var beforeExpiry = Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(expiring));

        Assert.NotNull(beforeExpiry.Value.ExpiresAt);
        Assert.True(beforeExpiry.Value.ExpiresAt > applied.StoreNow);
        Assert.Null(Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(durable)).Value.ExpiresAt);

        await Task.Delay(250);
        Assert.IsType<ZLinkStoreReadResult.Missing>(
            await store.ReadAsync(expiring));
        Assert.IsType<ZLinkStoreReadResult.Found>(
            await store.ReadAsync(durable));
    }

    [SkippableFact]
    public async Task Maximum_unique_key_batch_is_committed_atomically()
    {
        Skip.IfNot(fixture.RedisAvailable, fixture.SkipReason);
        await using var store = fixture.CreateStore();
        var mutations = Enumerable.Range(0, 2048)
            .Select(index => (ZLinkStoreMutation)new ZLinkStoreMutation.Put(
                new ZLinkStoreKey($"bound:{index:D4}"),
                new byte[] { (byte)(index & 0xff) },
                null))
            .ToArray();

        var result = Assert.IsType<ZLinkStoreWriteResult.Applied>(
            await store.WriteAsync(new ZLinkStoreWriteRequest([], mutations)));

        Assert.Equal(2048, result.PutVersions.Count);
        var page = Assert.IsType<ZLinkStoreScanResult.Page>(
            await store.ScanAsync(new ZLinkStoreScanRequest("bound:", null, 1000)));
        Assert.Equal(1000, page.Value.Items.Count);
        Assert.NotNull(page.Value.NextCursor);
    }

    [Fact]
    public async Task Provider_rejects_contract_bounds_before_connecting()
    {
        await using var store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions
            {
                ConnectionString = "unused:6379",
                KeyPrefix = "zlink:bounds"
            },
            _ => throw new InvalidOperationException(
                "Validation must finish before Redis I/O."));

        await Assert.ThrowsAsync<ArgumentException>(
            () => store.ReadAsync(new ZLinkStoreKey(string.Empty)).AsTask());
        await Assert.ThrowsAsync<ArgumentException>(
            () => store.WriteAsync(new ZLinkStoreWriteRequest(
                [],
                [
                    new ZLinkStoreMutation.Put(
                        new ZLinkStoreKey("too-large"),
                        new byte[(1024 * 1024) + 1],
                        null)
                ])).AsTask());
        await Assert.ThrowsAsync<ArgumentException>(
            () => store.WriteAsync(new ZLinkStoreWriteRequest(
                [],
                Enumerable.Range(0, 2049)
                    .Select(index => (ZLinkStoreMutation)
                        new ZLinkStoreMutation.Delete(
                            new ZLinkStoreKey($"too-many:{index}")))
                    .ToArray())).AsTask());
        await Assert.ThrowsAsync<ArgumentOutOfRangeException>(
            () => store.ScanAsync(new ZLinkStoreScanRequest("", null, 1001))
                .AsTask());
    }
}
