using System.Reflection;
using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisLocationStoreLifecycleTests
{
    [Fact]
    public async Task Dispose_Waits_For_First_Connect_And_Disposes_The_Published_Connection_Once()
    {
        var connectStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseConnect = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var connection = new TestRedisConnection();
        var store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions
            {
                ConnectionString = "unused:6379",
                KeyPrefix = "zlink:lifecycle-test"
            },
            async _ =>
            {
                connectStarted.SetResult();
                await releaseConnect.Task.ConfigureAwait(false);
                return connection;
            });

        var read = store.ReadAsync(new ZLinkStoreKey("lifecycle:read"))
            .AsTask();
        await connectStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var dispose = store.DisposeAsync().AsTask();
        Assert.False(dispose.IsCompleted);

        releaseConnect.SetResult();
        Assert.IsType<ZLinkStoreReadResult.Missing>(
            await read.WaitAsync(TimeSpan.FromSeconds(5)));
        await dispose.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(1, connection.DisposeCount);
        await Assert.ThrowsAsync<ObjectDisposedException>(
            () => store.ReadAsync(new ZLinkStoreKey("lifecycle:after-dispose"))
                .AsTask());

        await store.DisposeAsync();
        Assert.Equal(1, connection.DisposeCount);
    }

    [Fact]
    public async Task Concurrent_Dispose_Callers_Await_One_Blocked_Disposal_Transaction()
    {
        var releaseDispose = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var connection = new TestRedisConnection(releaseDispose.Task);
        var store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions
            {
                ConnectionString = "unused:6379",
                KeyPrefix = "zlink:concurrent-dispose-test"
            },
            _ => ValueTask.FromResult<IZLinkRedisConnection>(connection));

        Assert.IsType<ZLinkStoreReadResult.Missing>(
            await store.ReadAsync(new ZLinkStoreKey("lifecycle:connect")));

        var first = store.DisposeAsync().AsTask();
        await connection.DisposeStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
        var second = store.DisposeAsync().AsTask();

        Assert.Same(first, second);
        Assert.False(first.IsCompleted);
        Assert.False(second.IsCompleted);
        Assert.Equal(1, connection.DisposeCount);

        releaseDispose.SetResult();
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(1, connection.DisposeCount);
    }

    [Fact]
    public async Task Dispose_Stops_New_Admission_And_Waits_For_An_Admitted_Command()
    {
        var database = DispatchProxy.Create<IDatabase, BlockingRedisDatabaseProxy>();
        var command = (BlockingRedisDatabaseProxy)(object)database;
        var connection = new TestRedisConnection(database: database);
        var store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions
            {
                ConnectionString = "unused:6379",
                KeyPrefix = "zlink:operation-drain-test"
            },
            _ => ValueTask.FromResult<IZLinkRedisConnection>(connection));

        var admitted = store.ReadAsync(new ZLinkStoreKey("lifecycle:blocked"))
            .AsTask();
        await command.CommandStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var dispose = store.DisposeAsync().AsTask();
        Assert.False(dispose.IsCompleted);
        Assert.False(connection.DisposeStarted.Task.IsCompleted);
        await Assert.ThrowsAsync<ObjectDisposedException>(
            () => store.ReadAsync(new ZLinkStoreKey("lifecycle:rejected"))
                .AsTask());

        command.ReleaseCommand.TrySetResult(MissingReadResult());
        Assert.IsType<ZLinkStoreReadResult.Missing>(
            await admitted.WaitAsync(TimeSpan.FromSeconds(5)));
        await dispose.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(1, connection.DisposeCount);
    }

    private sealed class TestRedisConnection(
        Task? disposeBlock = null,
        IDatabase? database = null) : IZLinkRedisConnection
    {
        private readonly IDatabase _database = database ?? DispatchProxy.Create<IDatabase, RedisDatabaseProxy>();
        private int _disposeCount;

        public int DisposeCount => Volatile.Read(ref _disposeCount);

        public TaskCompletionSource DisposeStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public IDatabase GetDatabase() => _database;

        public async ValueTask DisposeAsync()
        {
            Interlocked.Increment(ref _disposeCount);
            DisposeStarted.TrySetResult();
            if (disposeBlock is not null) await disposeBlock.ConfigureAwait(false);
        }
    }

    private class RedisDatabaseProxy : DispatchProxy
    {
        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            if (targetMethod?.Name == nameof(IDatabase.ScriptEvaluateAsync)
                && targetMethod.ReturnType == typeof(Task<RedisResult>))
                return Task.FromResult(MissingReadResult());

            throw new NotSupportedException(targetMethod?.ToString());
        }
    }

    private class BlockingRedisDatabaseProxy : DispatchProxy
    {
        public TaskCompletionSource CommandStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource<RedisResult> ReleaseCommand { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        protected override object? Invoke(MethodInfo? targetMethod, object?[]? args)
        {
            if (targetMethod?.Name == nameof(IDatabase.ScriptEvaluateAsync)
                && targetMethod.ReturnType == typeof(Task<RedisResult>))
            {
                CommandStarted.TrySetResult();
                return ReleaseCommand.Task;
            }

            throw new NotSupportedException(targetMethod?.ToString());
        }
    }

    private static RedisResult MissingReadResult() =>
        RedisResult.Create(
        [
            RedisResult.Create((RedisValue)"missing"),
            RedisResult.Create((RedisValue)DateTimeOffset.UtcNow
                .ToUnixTimeMilliseconds())
        ]);
}
