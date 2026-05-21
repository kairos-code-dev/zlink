using System.Collections.Concurrent;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.UnitTests;

public sealed class SerialExecutorTests
{
    [Fact]
    public async Task StreamSessionSerialExecutor_Continues_After_Work_Exception()
    {
        var exceptions = new ConcurrentQueue<Exception>();
        ZLinkRuntimeErrorSink.UnhandledCallbackException += exceptions.Enqueue;
        try
        {
            await using var executor = new ZLinkStreamSessionSerialExecutor();
            var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

            Assert.True(executor.Enqueue(() => throw new InvalidOperationException("stream failure")));
            Assert.True(executor.Enqueue(() =>
            {
                completed.SetResult();
                return ValueTask.CompletedTask;
            }));

            await completed.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains(exceptions, static ex => ex.Message == "stream failure");
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= exceptions.Enqueue;
        }
    }

    [Fact]
    public async Task SpotSerialExecutor_Continues_After_Queued_Work_Exception()
    {
        var exceptions = new ConcurrentQueue<Exception>();
        ZLinkRuntimeErrorSink.UnhandledCallbackException += exceptions.Enqueue;
        try
        {
            await using var executor = new ZLinkSpotSerialExecutor(
                null!,
                static () => false,
                CancellationToken.None);
            var completed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

            executor.Queue(static (_, _) => throw new InvalidOperationException("spot failure"));
            executor.Queue((_, _) =>
            {
                completed.SetResult();
                return ValueTask.CompletedTask;
            });

            await completed.Task.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains(exceptions, static ex => ex.Message == "spot failure");
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= exceptions.Enqueue;
        }
    }

    [Fact]
    public async Task SpotSerialExecutor_ExecuteAsync_Propagates_Work_Exception()
    {
        var exceptions = new ConcurrentQueue<Exception>();
        ZLinkRuntimeErrorSink.UnhandledCallbackException += exceptions.Enqueue;
        try
        {
            await using var executor = new ZLinkSpotSerialExecutor(
                null!,
                static () => false,
                CancellationToken.None);

            var thrown = await Assert.ThrowsAsync<InvalidOperationException>(
                () => executor.ExecuteAsync(
                    static (_, _) => throw new InvalidOperationException("spot execute failure"),
                    CancellationToken.None).AsTask());

            Assert.Equal("spot execute failure", thrown.Message);
            Assert.Contains(exceptions, static ex => ex.Message == "spot execute failure");
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= exceptions.Enqueue;
        }
    }

    [Fact]
    public async Task SerialExecutionQueue_RunAsync_Propagates_Work_Exception()
    {
        var exceptions = new ConcurrentQueue<Exception>();
        ZLinkRuntimeErrorSink.UnhandledCallbackException += exceptions.Enqueue;
        try
        {
            await using var queue = CreateQueue(CancellationToken.None);

            var thrown = await Assert.ThrowsAsync<InvalidOperationException>(
                () => queue.RunAsync(
                    static _ => throw new InvalidOperationException("queue failure"),
                    CancellationToken.None).AsTask());

            Assert.Equal("queue failure", thrown.Message);
            Assert.Contains(exceptions, static ex => ex.Message == "queue failure");
        }
        finally
        {
            ZLinkRuntimeErrorSink.UnhandledCallbackException -= exceptions.Enqueue;
        }
    }

    [Fact]
    public async Task SerialExecutionQueue_Wait_Cancellation_Does_Not_Remove_Queued_Work()
    {
        await using var queue = CreateQueue(CancellationToken.None);
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var secondRan = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var first = queue.RunAsync(
            async _ =>
            {
                firstStarted.SetResult();
                await releaseFirst.Task.ConfigureAwait(false);
            },
            CancellationToken.None).AsTask();
        await firstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        using var waitCancellation = new CancellationTokenSource();
        var second = queue.RunAsync(
            _ =>
            {
                secondRan.SetResult();
                return ValueTask.CompletedTask;
            },
            waitCancellation.Token).AsTask();
        waitCancellation.Cancel();

        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(
                () => second.WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.False(secondRan.Task.IsCompleted);
        }
        finally
        {
            releaseFirst.SetResult();
        }

        await first.WaitAsync(TimeSpan.FromSeconds(5));
        await secondRan.Task.WaitAsync(TimeSpan.FromSeconds(5));
    }

    [Fact]
    public async Task SerialExecutionQueue_TryPost_ReturnsFalse_WhenQueueIsFull()
    {
        await using var queue = CreateQueue(CancellationToken.None, capacity: 1);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        Assert.True(queue.TryPost(
            async _ => await releaseFirst.Task.ConfigureAwait(false),
            out _));
        Assert.False(queue.TryPost(
            _ => ValueTask.CompletedTask,
            out _));

        releaseFirst.SetResult();
    }

    [Fact]
    public async Task SerialExecutionQueue_PostAsync_Throws_WhenQueueIsFull()
    {
        await using var queue = CreateQueue(CancellationToken.None, capacity: 1);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        Assert.True(queue.TryPost(
            async _ => await releaseFirst.Task.ConfigureAwait(false),
            out _));

        await Assert.ThrowsAsync<InvalidOperationException>(
            () => queue.PostAsync(
                _ => ValueTask.CompletedTask,
                CancellationToken.None).AsTask());

        releaseFirst.SetResult();
    }

    [Fact]
    public async Task ActorDispatchCancellation_Does_Not_Stop_Current_Or_Later_Dispatch()
    {
        var state = new ZLinkActorRuntimeState("test-actor");
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var secondRan = false;
        var thirdRan = false;

        var first = state.ExecuteDispatchAsync(
            CreateHeader("first"),
            async _ =>
            {
                firstStarted.SetResult();
                await releaseFirst.Task.ConfigureAwait(false);
            },
            CancellationToken.None).AsTask();
        await firstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        using var secondCancellation = new CancellationTokenSource();
        var second = state.ExecuteDispatchAsync(
            CreateHeader("second"),
            _ =>
            {
                secondRan = true;
                return ValueTask.CompletedTask;
            },
            secondCancellation.Token).AsTask();
        await secondCancellation.CancelAsync();

        await Assert.ThrowsAsync<OperationCanceledException>(
            () => second.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.False(secondRan);

        releaseFirst.SetResult();
        await first.WaitAsync(TimeSpan.FromSeconds(5));

        await state.ExecuteDispatchAsync(
            CreateHeader("third"),
            _ =>
            {
                thirdRan = true;
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(thirdRan);
    }

    [Fact]
    public async Task ActorDispatchMailbox_Runs_Waiters_In_Fifo_Order()
    {
        var state = new ZLinkActorRuntimeState("test-actor");
        var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var order = new List<string>();

        var first = state.ExecuteDispatchAsync(
            CreateHeader("first"),
            async _ =>
            {
                order.Add("first");
                firstStarted.SetResult();
                await releaseFirst.Task.ConfigureAwait(false);
            },
            CancellationToken.None).AsTask();
        await firstStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var second = state.ExecuteDispatchAsync(
            CreateHeader("second"),
            _ =>
            {
                order.Add("second");
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();
        var third = state.ExecuteDispatchAsync(
            CreateHeader("third"),
            _ =>
            {
                order.Add("third");
                return ValueTask.CompletedTask;
            },
            CancellationToken.None).AsTask();

        releaseFirst.SetResult();

        await Task.WhenAll(first, second, third).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(["first", "second", "third"], order);
    }

    private static ZlinkStreamHeader CreateHeader(string name)
    {
        return new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            name,
            ZlinkStreamMetadata.Empty);
    }

    private static ZLinkSerialExecutionQueue CreateQueue(
        CancellationToken executionToken,
        int capacity = 4096)
    {
        var errorSink = new ZLinkRuntimeErrorSink();
        return new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, executionToken),
            errorSink,
            executionToken,
            capacity);
    }
}
