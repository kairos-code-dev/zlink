using System.Collections.Concurrent;

namespace Zlink.Framework.UnitTests;

public sealed class WorkerPoolTests
{
    [Fact]
    public async Task RunWorker_Async_Returns_Result_From_Pool_Thread()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();

        var call = CreateCall(pool, _ => Environment.CurrentManagedThreadId, queue);
        var workerThreadId = await call.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));

        Assert.NotEqual(Environment.CurrentManagedThreadId, workerThreadId);
    }

    [Fact]
    public async Task RunWorker_Submit_Posts_Completion_To_Dispatcher_Not_Worker_Thread()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();
        var completed = new TaskCompletionSource<int>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        int workerThreadId = 0;

        var call = CreateCall(
            pool,
            _ =>
            {
                workerThreadId = Environment.CurrentManagedThreadId;
                return 42;
            },
            queue);
        call.Submit((result, _) =>
        {
            // The callback must run inside a dispatcher queue item, not
            // directly on the pool thread that produced the result.
            completed.TrySetResult(
                Environment.CurrentManagedThreadId == workerThreadId
                    ? -1
                    : result);
            return ValueTask.CompletedTask;
        });

        var observed = await completed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(42, observed);
    }

    [Fact]
    public async Task RunWorker_Queue_Full_Fails_Fast_With_WorkerQueueFull()
    {
        using var pool = CreatePool(maxThreads: 1, maxQueueLength: 1);
        await using var queue = CreateQueue();
        using var blockPool = new ManualResetEventSlim(false);

        try
        {
            // Occupy the single pool thread, then fill the single queue slot.
            _ = CreateCall(pool, _ => { blockPool.Wait(); return 0; }, queue).Async();
            await WaitForAsync(() => pool.ThreadCount == 1);
            _ = CreateCall(pool, _ => 0, queue).Async();
            await WaitForAsync(() => pool.QueueLength == 1);

            var overflow = CreateCall(pool, _ => 0, queue).Async();
            var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                async () => await overflow.AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.Equal(ZLinkFrameworkErrorKind.WorkerQueueFull, error.Kind);
            Assert.True(error.IsRetriable);
        }
        finally
        {
            blockPool.Set();
        }
    }

    [Fact]
    public async Task RunWorker_Timeout_Drops_Late_Completion()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();
        using var releaseWork = new ManualResetEventSlim(false);
        var callbacks = new ConcurrentQueue<string>();
        var failed = new TaskCompletionSource<Exception>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        var call = CreateCall(
            pool,
            _ =>
            {
                releaseWork.Wait();
                return 7;
            },
            queue);
        call.Timeout(TimeSpan.FromMilliseconds(100));
        call.Submit(
            (result, _) =>
            {
                callbacks.Enqueue($"completed:{result}");
                return ValueTask.CompletedTask;
            },
            (error, _) =>
            {
                callbacks.Enqueue($"error:{((ZLinkFrameworkException)error).Kind}");
                failed.TrySetResult(error);
                return ValueTask.CompletedTask;
            });

        var observed = await failed.Task.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(
            ZLinkFrameworkErrorKind.WorkerTimedOut,
            ((ZLinkFrameworkException)observed).Kind);

        // Let the abandoned work finish; its late completion must be dropped.
        releaseWork.Set();
        await Task.Delay(300);
        Assert.DoesNotContain(callbacks, c => c.StartsWith("completed:", StringComparison.Ordinal));
        Assert.Single(callbacks);
    }

    [Fact]
    public async Task RunWorker_Idle_Threads_Shrink_After_Idle_Timeout()
    {
        using var pool = new ZLinkWorkerPool(
            minThreads: 0,
            maxThreads: 2,
            idleTimeout: TimeSpan.FromMilliseconds(150),
            maxQueueLength: 16);
        await using var queue = CreateQueue();

        await CreateCall(pool, _ => 1, queue).Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(pool.ThreadCount >= 1);

        await WaitForAsync(() => pool.ThreadCount == 0, timeoutMs: 5_000);
        Assert.Equal(0, pool.ThreadCount);
    }

    [Fact]
    public async Task RunWorker_Detached_Callback_Observes_State_Mutated_While_Waiting()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();
        using var releaseWork = new ManualResetEventSlim(false);
        var observed = new TaskCompletionSource<string>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        // Spot-owned state that another serial-line callback mutates while
        // the worker job is still running.
        var spotState = "initial";

        var call = CreateCall(
            pool,
            _ =>
            {
                releaseWork.Wait();
                return 1;
            },
            queue);
        call.Submit((_, _) =>
        {
            // Detached completion is explicit interleaving opt-in: the state
            // it sees may differ from the state at submit time.
            observed.TrySetResult(spotState);
            return ValueTask.CompletedTask;
        });

        Assert.True(queue.TryPost(
            _ =>
            {
                spotState = "mutated";
                return ValueTask.CompletedTask;
            },
            out _));
        await WaitForAsync(() => spotState == "mutated");
        releaseWork.Set();

        Assert.Equal("mutated", await observed.Task.WaitAsync(TimeSpan.FromSeconds(5)));
    }

    [Fact]
    public async Task RunWorker_Yield_Allows_Dispatcher_Work_While_Worker_Runs()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();
        using var releaseWork = new ManualResetEventSlim(false);
        var workerStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var firstResumed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirstResume = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thirdRan = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var order = new ConcurrentQueue<string>();

        var first = queue.RunAsync(
            async ct =>
            {
                order.Enqueue("first-start");
                var call = CreateCall(
                    pool,
                    _ =>
                    {
                        workerStarted.SetResult();
                        releaseWork.Wait();
                        return 42;
                    },
                    queue);
                var result = await call.Yield(ct).ConfigureAwait(false);
                order.Enqueue($"first-resumed:{result}");
                firstResumed.SetResult();
                await releaseFirstResume.Task.ConfigureAwait(false);
            },
            CancellationToken.None).AsTask();

        await workerStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        await queue.RunAsync(
            _ =>
            {
                order.Enqueue("second");
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask().WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(new[] { "first-start", "second" }, order.ToArray());

        releaseWork.Set();
        await firstResumed.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var third = queue.RunAsync(
            _ =>
            {
                order.Enqueue("third");
                thirdRan.SetResult();
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();

        await Task.Delay(100);
        Assert.False(thirdRan.Task.IsCompleted);

        releaseFirstResume.SetResult();
        await Task.WhenAll(first, third).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(new[] { "first-start", "second", "first-resumed:42", "third" }, order.ToArray());
    }

    [Fact]
    public async Task RunWorker_Yield_Requires_Captured_Turn()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();
        var call = CreateCall(pool, _ => 1, queue);

        var error = await Assert.ThrowsAsync<InvalidOperationException>(
            () => call.Yield().AsTask());

        Assert.Contains("captured", error.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task RunWorker_Worker_Exception_Maps_To_WorkerFailed()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();

        var call = CreateCall<int>(
            pool,
            _ => throw new InvalidOperationException("boom"),
            queue);
        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
            async () => await call.Async().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));

        Assert.Equal(ZLinkFrameworkErrorKind.WorkerFailed, error.Kind);
        Assert.False(error.IsRetriable);
        Assert.IsType<InvalidOperationException>(error.InnerException);
    }

    [Fact]
    public async Task RunWorker_Second_Terminator_Throws()
    {
        using var pool = CreatePool(maxThreads: 2);
        await using var queue = CreateQueue();

        var call = CreateCall(pool, _ => 1, queue);
        _ = call.Async();
        Assert.Throws<InvalidOperationException>(
            () => call.Submit((_, _) => ValueTask.CompletedTask));
    }

    private static ZLinkWorkerPool CreatePool(
        int maxThreads,
        int maxQueueLength = 16)
        => new(
            minThreads: 0,
            maxThreads: maxThreads,
            idleTimeout: TimeSpan.FromSeconds(30),
            maxQueueLength: maxQueueLength);

    private static ZLinkWorkerCall<TResult> CreateCall<TResult>(
        ZLinkWorkerPool pool,
        Func<CancellationToken, TResult> work,
        ZLinkSerialExecutionQueue dispatcherQueue)
        => new(
            pool,
            work,
            callback => dispatcherQueue.TryPost(callback, out _));

    private static ZLinkSerialExecutionQueue CreateQueue()
    {
        var errorSink = new ZLinkRuntimeErrorSink();
        return new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, CancellationToken.None),
            errorSink,
            CancellationToken.None);
    }

    private static async Task WaitForAsync(Func<bool> predicate, int timeoutMs = 5_000)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (!predicate())
        {
            if (Environment.TickCount64 > deadline)
            {
                throw new TimeoutException("Condition was not reached in time.");
            }

            await Task.Delay(10);
        }
    }
}
