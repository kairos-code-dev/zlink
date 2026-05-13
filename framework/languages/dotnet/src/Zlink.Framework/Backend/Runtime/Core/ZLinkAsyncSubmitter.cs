using System.Collections.Generic;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkAsyncSubmitter : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;

    private readonly object _gate = new();
    private readonly Queue<PendingSubmit> _pending = new();
    private readonly int _capacity;
    private readonly TimeSpan? _sendTimeout;
    private readonly CancellationToken _stopToken;
    private bool _draining;
    private bool _disposed;

    public ZLinkAsyncSubmitter(
        Action<Action> registerReadyHandler,
        TimeSpan? sendTimeout,
        CancellationToken stopToken,
        int capacity = DefaultCapacity)
    {
        _capacity = capacity > 0 ? capacity : throw new ArgumentOutOfRangeException(nameof(capacity));
        _sendTimeout = ValidateTimeout(sendTimeout);
        _stopToken = stopToken;
        registerReadyHandler(OnSendReady);
    }

    public ValueTask SubmitAsync(
        Message message,
        Func<Message, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(trySubmit);
        return SubmitAsync(
            new[] { message },
            parts => trySubmit(parts[0]),
            cancellationToken);
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

        var deadline = ResolveDeadline();
        if (deadline is DateTimeOffset nowDeadline && nowDeadline <= DateTimeOffset.UtcNow)
        {
            DisposeParts(parts);
            throw new TimeoutException("ZLink async submit timed out before the socket became writable.");
        }

        var pending = PendingSubmit.CreateCommand(parts, trySubmit, deadline, Drain);
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
        return SubmitRequestAsync<T>(
            new[] { message },
            (parts, complete, fail) => trySubmit(parts[0], complete, fail),
            cancellationToken);
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

        var deadline = ResolveDeadline();
        if (deadline is DateTimeOffset nowDeadline && nowDeadline <= DateTimeOffset.UtcNow)
        {
            DisposeParts(parts);
            throw new TimeoutException("ZLink async submit timed out before the socket became writable.");
        }

        var pendingSubmit = PendingSubmit.CreateRequest(parts, Submit, deadline, Drain, completion);
        EnqueuePending(pendingSubmit, cancellationToken);
        return AwaitResultAsync<T>(pendingSubmit.Task);
    }

    public ValueTask DisposeAsync()
    {
        Queue<PendingSubmit> remaining;
        lock (_gate)
        {
            if (_disposed)
            {
                return ValueTask.CompletedTask;
            }

            _disposed = true;
            remaining = new Queue<PendingSubmit>(_pending);
            _pending.Clear();
        }

        foreach (var item in remaining)
        {
            item.TryFail(new ObjectDisposedException(nameof(ZLinkAsyncSubmitter)));
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
                ThrowIfDisposed();
                if (_pending.Count >= _capacity)
                {
                    throw new InvalidOperationException("ZLink async submit queue is full.");
                }

                _pending.Enqueue(pending);
            }

            pending.Activate(cancellationToken, _stopToken);
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
            if (_draining || _disposed)
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
                lock (_gate)
                {
                    item = _pending.Count > 0 ? _pending.Peek() : null;
                    if (item is null)
                    {
                        return;
                    }
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
        lock (_gate)
        {
            if (_pending.Count > 0 && ReferenceEquals(_pending.Peek(), expected))
            {
                _pending.Dequeue().Dispose();
            }
        }
    }

    private DateTimeOffset? ResolveDeadline()
    {
        return _sendTimeout is null
            ? null
            : DateTimeOffset.UtcNow.Add(_sendTimeout.Value);
    }

    private void ThrowIfDisposed()
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(ZLinkAsyncSubmitter));
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

}
