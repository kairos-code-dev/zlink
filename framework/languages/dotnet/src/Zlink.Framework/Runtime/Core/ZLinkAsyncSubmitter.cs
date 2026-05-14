namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkAsyncSubmitter : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;

    private readonly object _gate = new();
    private readonly ZLinkSubmitQueue _pending;
    private readonly TimeSpan? _sendTimeout;
    private readonly CancellationToken _stopToken;
    private readonly ZLinkSubmitOperationFactory _operationFactory;
    private bool _draining;

    public ZLinkAsyncSubmitter(
        Action<Action> registerReadyHandler,
        TimeSpan? sendTimeout,
        CancellationToken stopToken,
        int capacity = DefaultCapacity)
    {
        _pending = new ZLinkSubmitQueue(capacity);
        _sendTimeout = ValidateTimeout(sendTimeout);
        _stopToken = stopToken;
        _operationFactory = new ZLinkSubmitOperationFactory(_sendTimeout, Drain);
        registerReadyHandler(OnSendReady);
    }

    public ValueTask SubmitAsync(
        Message message,
        Func<Message, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(trySubmit);

        cancellationToken.ThrowIfCancellationRequested();
        _stopToken.ThrowIfCancellationRequested();

        if (TrySubmitNow(message, trySubmit))
        {
            message.Dispose();
            return ValueTask.CompletedTask;
        }

        var pending = _operationFactory.CreateCommand(
            new SingleMessageParts(message),
            parts => trySubmit(parts[0]));
        EnqueuePending(pending, cancellationToken);
        return new ValueTask(pending.Task);
    }

    public ValueTask SubmitAsync(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(parts);
        ArgumentNullException.ThrowIfNull(trySubmit);
        EnsureNotEmpty(parts);

        cancellationToken.ThrowIfCancellationRequested();
        _stopToken.ThrowIfCancellationRequested();

        if (TrySubmitNow(parts, trySubmit))
        {
            DisposeParts(parts);
            return ValueTask.CompletedTask;
        }

        var pending = _operationFactory.CreateCommand(parts, trySubmit);
        EnqueuePending(pending, cancellationToken);
        return new ValueTask(pending.Task);
    }

    public ValueTask<T> SubmitRequestAsync<T>(
        Message message,
        Func<Message, Action<T>, Action<Exception>, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(trySubmit);

        cancellationToken.ThrowIfCancellationRequested();
        _stopToken.ThrowIfCancellationRequested();

        var completion = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        bool Submit(Message pending)
        {
            return trySubmit(
                pending,
                result => completion.TrySetResult(result),
                exception => completion.TrySetException(exception));
        }

        if (TrySubmitNow(message, Submit))
        {
            message.Dispose();
            return AwaitResultAsync<T>(completion.Task);
        }

        var parts = new SingleMessageParts(message);
        var pendingSubmit = _operationFactory.CreateRequest(
            parts,
            pending => Submit(pending[0]),
            completion);
        EnqueuePending(pendingSubmit, cancellationToken);
        return AwaitResultAsync<T>(pendingSubmit.Task);
    }

    public ValueTask<T> SubmitRequestAsync<T>(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, Action<T>, Action<Exception>, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(parts);
        ArgumentNullException.ThrowIfNull(trySubmit);
        EnsureNotEmpty(parts);

        cancellationToken.ThrowIfCancellationRequested();
        _stopToken.ThrowIfCancellationRequested();

        var completion = new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously);
        bool Submit(IReadOnlyList<Message> pending)
        {
            return trySubmit(
                pending,
                result => completion.TrySetResult(result),
                exception => completion.TrySetException(exception));
        }

        if (TrySubmitNow(parts, Submit))
        {
            DisposeParts(parts);
            return AwaitResultAsync<T>(completion.Task);
        }

        var pendingSubmit = _operationFactory.CreateRequest(parts, Submit, completion);
        EnqueuePending(pendingSubmit, cancellationToken);
        return AwaitResultAsync<T>(pendingSubmit.Task);
    }

    public ValueTask DisposeAsync()
    {
        var remaining = _pending.DisposeAll();

        foreach (var item in remaining)
        {
            try
            {
                item.TryFail(new ObjectDisposedException(nameof(ZLinkAsyncSubmitter)));
            }
            finally
            {
                item.Dispose();
            }
        }

        return ValueTask.CompletedTask;
    }

    private void OnSendReady()
    {
        Drain();
    }

    private void EnqueuePending(PendingSubmit pending, CancellationToken cancellationToken)
    {
        try
        {
            lock (_gate)
            {
                _pending.Enqueue(pending);
            }

            pending.Activate(cancellationToken, _stopToken);
            Drain();
        }
        catch
        {
            pending.Dispose();
            throw;
        }
    }

    private void Drain()
    {
        lock (_gate)
        {
            if (_draining)
            {
                return;
            }

            _draining = true;
        }

        try
        {
            while (true)
            {
                PendingSubmit? item;
                _pending.TryPeek(out item);
                if (item is null)
                {
                    return;
                }

                if (item.IsCompleted)
                {
                    Dequeue(item);
                    continue;
                }

                if (item.Deadline is DateTimeOffset deadline && deadline <= DateTimeOffset.UtcNow)
                {
                    item.TryFail(new TimeoutException("ZLink async submit timed out before the socket became writable."));
                    Dequeue(item);
                    continue;
                }

                if (!TrySubmitNow(item.Parts, item.TrySubmit))
                {
                    return;
                }

                if (item.CompleteOnAccepted)
                {
                    item.TryComplete(null);
                }

                Dequeue(item);
            }
        }
        finally
        {
            lock (_gate)
            {
                _draining = false;
            }
        }
    }

    private bool TrySubmitNow(IReadOnlyList<Message> parts, Func<IReadOnlyList<Message>, bool> trySubmit)
    {
        try
        {
            return trySubmit(parts);
        }
        catch (ZlinkException error) when (IsRetryableSubmitFailure(error))
        {
            return false;
        }
    }

    private bool TrySubmitNow(Message message, Func<Message, bool> trySubmit)
    {
        try
        {
            return trySubmit(message);
        }
        catch (ZlinkException error) when (IsRetryableSubmitFailure(error))
        {
            return false;
        }
    }

    private static bool IsRetryableSubmitFailure(ZlinkException error)
    {
        if (error is ZlinkSubmitException
            {
                Result: ZlinkSubmitException.ErrorCode.Backpressured
                or ZlinkSubmitException.ErrorCode.NotConnected
            })
        {
            return true;
        }

        return false;
    }

    private void Dequeue(PendingSubmit expected)
    {
        if (_pending.TryDequeue(expected, out var pending) && pending is not null)
        {
            pending.Dispose();
        }
    }

    private static TimeSpan? ValidateTimeout(TimeSpan? timeout)
    {
        if (timeout is { } value && value < TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), "SendTimeout must be null, zero, or a positive duration.");
        }

        return timeout;
    }

    private static async ValueTask<T> AwaitResultAsync<T>(Task<object?> task)
    {
        return (T)(await task.ConfigureAwait(false))!;
    }

    private static void EnsureNotEmpty(IReadOnlyList<Message> parts)
    {
        if (parts.Count == 0)
        {
            throw new ArgumentException("At least one message part is required.", nameof(parts));
        }
    }

    private static void DisposeParts(IReadOnlyList<Message> parts)
    {
        foreach (var part in parts)
        {
            part.Dispose();
        }
    }

    private sealed class SingleMessageParts(Message message) : IReadOnlyList<Message>
    {
        public int Count => 1;

        public Message this[int index] => index == 0
            ? message
            : throw new ArgumentOutOfRangeException(nameof(index));

        public IEnumerator<Message> GetEnumerator()
        {
            yield return message;
        }

        System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}
