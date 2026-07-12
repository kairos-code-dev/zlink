namespace Zlink.Framework.UnitTests;

public sealed class ZLinkAsyncSubmitterTests
{
    [Fact]
    public async Task Async_DrainsPendingItemFromReadyCallback()
    {
        Action? ready = null;
        var writable = false;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            _ =>
            {
                submitted++;
                return writable;
            });

        Assert.False(task.IsCompleted);
        Assert.Equal(2, submitted);

        writable = true;
        ready?.Invoke();
        await task;

        Assert.Equal(3, submitted);
    }

    [Fact]
    public async Task Async_RetriesAfterQueueingToCloseReadyRace()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);
        var submitted = 0;

        await submitter.Async(
            Message.From("payload"),
            _ =>
            {
                submitted++;
                return submitted == 2;
            });

        Assert.Equal(2, submitted);
    }

    [Fact]
    public async Task Async_RetriesWithFreshMessageCopies()
    {
        Action? ready = null;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            message =>
            {
                submitted++;
                Assert.Equal("payload", message.GetString());
                return submitted == 3;
            });

        Assert.False(task.IsCompleted);
        Assert.Equal(2, submitted);

        ready?.Invoke();
        await task;

        Assert.Equal(3, submitted);
    }

    [Fact]
    public async Task Async_RetainsOriginalWhenRetryableAttemptConsumesMessage()
    {
        Action? ready = null;
        var submitted = 0;

        await using var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            message =>
            {
                submitted++;
                Assert.Equal("payload", message.GetString());
                if (submitted < 3)
                {
                    message.Dispose();
                    return false;
                }

                return true;
            });

        Assert.False(task.IsCompleted);
        Assert.Equal(2, submitted);

        ready?.Invoke();
        await task;

        Assert.Equal(3, submitted);
    }

    [Fact]
    public async Task Async_FailsPendingItemWhenSendTimeoutExpires()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromMilliseconds(20),
            CancellationToken.None);

        var task = submitter.Async(
            Message.From("payload"),
            _ => false);

        await Assert.ThrowsAsync<TimeoutException>(async () => await task.AsTask());
    }

    [Fact]
    public async Task Async_ThrowsWhenQueueIsFull()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None,
            1);

        var first = submitter.Async(
            Message.From("first"),
            _ => false);

        Assert.False(first.IsCompleted);

        Assert.Throws<InvalidOperationException>(() =>
            submitter.Async(
                Message.From("second"),
                _ => false));
    }

    [Fact]
    public async Task Async_Throws_NonRetryable_Submit_Failure_On_The_Caller()
    {
        await using var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None,
            failFastNotConnected: static () => true);

        var exception = Assert.Throws<ZLinkFrameworkException>(() =>
            submitter.Async(
                Message.From("payload"),
                _ => throw new ZlinkSubmitException(ZlinkSubmitException.ErrorCode.NotConnected)));

        Assert.Equal(ZLinkFrameworkErrorKind.RouteNotConnected, exception.Kind);
    }

    [Fact]
    public async Task DisposeAsync_FailsPendingItems()
    {
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var pending = submitter.Async(
            Message.From("payload"),
            _ => false);

        await submitter.DisposeAsync();

        await Assert.ThrowsAsync<ObjectDisposedException>(async () => await pending.AsTask());
    }

    [Fact]
    public async Task DisposeAsync_WaitsForBlockedImmediateNativeSubmit_AndIsShared()
    {
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var submitter = new ZLinkAsyncSubmitter(
            _ => { },
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var submit = Task.Run(async () =>
            await submitter.Async(
                Message.From("payload"),
                message =>
                {
                    Assert.Equal("payload", message.GetString());
                    entered.TrySetResult();
                    release.Task.GetAwaiter().GetResult();
                    Assert.Equal("payload", message.GetString());
                    return true;
                }));

        await entered.Task;
        var firstDispose = submitter.DisposeAsync().AsTask();
        var secondDispose = submitter.DisposeAsync().AsTask();

        Assert.Same(firstDispose, secondDispose);
        Assert.False(firstDispose.IsCompleted);

        release.TrySetResult();
        await submit;
        await firstDispose;
    }

    [Fact]
    public async Task DisposeAsync_WaitsForBlockedDrainNativeSubmit_BeforeDisposingQueuedParts()
    {
        Action? ready = null;
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var attempts = 0;
        var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);

        var pending = submitter.Async(
            Message.From("payload"),
            message =>
            {
                Assert.Equal("payload", message.GetString());
                if (Interlocked.Increment(ref attempts) < 3) return false;

                entered.TrySetResult();
                release.Task.GetAwaiter().GetResult();
                Assert.Equal("payload", message.GetString());
                return false;
            });

        Assert.NotNull(ready);
        var drain = Task.Run(ready);
        await entered.Task;

        var dispose = submitter.DisposeAsync().AsTask();
        Assert.False(dispose.IsCompleted);

        release.TrySetResult();
        await drain;
        await dispose;
        await Assert.ThrowsAsync<ObjectDisposedException>(() => pending.AsTask());
        Assert.Equal(3, attempts);
    }

    [Fact]
    public async Task SubmitRequestAsync_ImmediateAcceptedRaw_CancellationEndsWaitAndDisposesLateReply()
    {
        await using var submitter = CreateSubmitter(out _);
        using var cancellation = new CancellationTokenSource();
        Action<IReadOnlyList<Message>>? complete = null;

        var request = submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            Message.From("request"),
            (_, onResult, _) =>
            {
                complete = onResult;
                return true;
            },
            cancellation.Token,
            ZLinkMessageParts.DisposeAll);

        cancellation.Cancel();
        var error = await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request.AsTask());
        Assert.Equal(cancellation.Token, error.CancellationToken);

        using var lateReply = Message.From("late");
        Assert.NotNull(complete);
        complete([lateReply]);
        Assert.Throws<ObjectDisposedException>(() => lateReply.AsReadOnlySpan());
    }

    [Fact]
    public async Task SubmitRequestAsync_QueuedThenAcceptedRaw_CancellationEndsWaitAndDisposesLateReply()
    {
        await using var submitter = CreateSubmitter(out var signalReady);
        using var cancellation = new CancellationTokenSource();
        Action<IReadOnlyList<Message>>? complete = null;
        var writable = false;

        var request = submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            Message.From("request"),
            (_, onResult, _) =>
            {
                if (!writable) return false;
                complete = onResult;
                return true;
            },
            cancellation.Token,
            ZLinkMessageParts.DisposeAll);

        Assert.False(request.IsCompleted);
        writable = true;
        signalReady();
        Assert.NotNull(complete);

        cancellation.Cancel();
        var error = await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request.AsTask());
        Assert.Equal(cancellation.Token, error.CancellationToken);

        using var lateReply = Message.From("late");
        complete([lateReply]);
        Assert.Throws<ObjectDisposedException>(() => lateReply.AsReadOnlySpan());
    }

    [Fact]
    public async Task SubmitRequestAsync_ImmediateAcceptedEnvelope_CancellationDisposesLateNativeReply()
    {
        await using var submitter = CreateSubmitter(out _);
        using var cancellation = new CancellationTokenSource();
        Action<RequestResult, IReadOnlyList<Message>>? nativeComplete = null;

        var request = submitter.SubmitRequestAsync<string>(
            Message.From("request"),
            (_, complete, fail) =>
            {
                nativeComplete = (result, reply) => ZLinkEnvelopeReplyCompletion.Complete(
                    result, reply, complete, fail, "test request");
                return true;
            },
            cancellation.Token);

        cancellation.Cancel();
        var error = await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request.AsTask());
        Assert.Equal(cancellation.Token, error.CancellationToken);

        var lateReply = CreateEnvelopeReply("late");
        Assert.NotNull(nativeComplete);
        nativeComplete(RequestResult.Ok, lateReply);
        Assert.All(lateReply, part =>
            Assert.Throws<ObjectDisposedException>(() => part.AsReadOnlySpan()));
    }

    [Fact]
    public async Task SubmitRequestAsync_QueuedThenAcceptedEnvelope_CancellationDisposesLateNativeReply()
    {
        await using var submitter = CreateSubmitter(out var signalReady);
        using var cancellation = new CancellationTokenSource();
        Action<RequestResult, IReadOnlyList<Message>>? nativeComplete = null;
        var writable = false;

        var request = submitter.SubmitRequestAsync<string>(
            Message.From("request"),
            (_, complete, fail) =>
            {
                if (!writable) return false;
                nativeComplete = (result, reply) => ZLinkEnvelopeReplyCompletion.Complete(
                    result, reply, complete, fail, "test request");
                return true;
            },
            cancellation.Token);

        Assert.False(request.IsCompleted);
        writable = true;
        signalReady();
        Assert.NotNull(nativeComplete);

        cancellation.Cancel();
        var error = await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request.AsTask());
        Assert.Equal(cancellation.Token, error.CancellationToken);

        var lateReply = CreateEnvelopeReply("late");
        nativeComplete(RequestResult.Ok, lateReply);
        Assert.All(lateReply, part =>
            Assert.Throws<ObjectDisposedException>(() => part.AsReadOnlySpan()));
    }

    [Fact]
    public async Task SubmitRequestAsync_LinkedTimeoutCancellationPreservesTokenAndDisposesLateReply()
    {
        await using var submitter = CreateSubmitter(out _);
        using var callerCancellation = new CancellationTokenSource();
        using var timeout = new CancellationTokenSource();
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(
            callerCancellation.Token,
            timeout.Token);
        Action<IReadOnlyList<Message>>? complete = null;

        var request = submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            Message.From("request"),
            (_, onResult, _) =>
            {
                complete = onResult;
                return true;
            },
            linked.Token,
            ZLinkMessageParts.DisposeAll);

        timeout.Cancel();
        var error = await Assert.ThrowsAnyAsync<OperationCanceledException>(() => request.AsTask());
        Assert.Equal(linked.Token, error.CancellationToken);

        using var lateReply = Message.From("late");
        Assert.NotNull(complete);
        complete([lateReply]);
        Assert.Throws<ObjectDisposedException>(() => lateReply.AsReadOnlySpan());
    }

    [Fact]
    public async Task SubmitRequestAsync_NormalWinnerTransfersRawReplyAndDisposesDuplicate()
    {
        await using var submitter = CreateSubmitter(out _);
        using var cancellation = new CancellationTokenSource();
        Action<IReadOnlyList<Message>>? complete = null;

        var request = submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            Message.From("request"),
            (_, onResult, _) =>
            {
                complete = onResult;
                return true;
            },
            cancellation.Token,
            ZLinkMessageParts.DisposeAll);

        using var winner = Message.From("winner");
        Assert.NotNull(complete);
        complete([winner]);
        var result = await request;
        cancellation.Cancel();

        Assert.Same(winner, Assert.Single(result));
        Assert.Equal("winner", winner.GetString());

        using var duplicate = Message.From("duplicate");
        complete([duplicate]);
        Assert.Throws<ObjectDisposedException>(() => duplicate.AsReadOnlySpan());
    }

    private static ZLinkAsyncSubmitter CreateSubmitter(out Action signalReady)
    {
        Action? ready = null;
        var submitter = new ZLinkAsyncSubmitter(
            handler => ready = handler,
            TimeSpan.FromSeconds(1),
            CancellationToken.None);
        signalReady = () =>
        {
            Assert.NotNull(ready);
            ready();
        };
        return submitter;
    }

    private static IReadOnlyList<Message> CreateEnvelopeReply(string body)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            "test",
            "Reply",
            ZLinkEnvelopeCodec.DefaultContentType,
            "correlation",
            null,
            null,
            null,
            null);
        return ZLinkEnvelopeCodec.EncodeParts(header, body, typeof(string), null);
    }
}
