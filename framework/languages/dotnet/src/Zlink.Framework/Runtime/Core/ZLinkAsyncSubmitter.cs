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

        cancellationToken.ThrowIfCancellationRequested();
        _stopToken.ThrowIfCancellationRequested();

        if (TrySubmitNow(message, trySubmit))
        {
            message.Dispose();
            return ValueTask.CompletedTask;
        }

        var deadline = ResolveDeadline();
        if (deadline is DateTimeOffset nowDeadline && nowDeadline <= DateTimeOffset.UtcNow)
        {
            message.Dispose();
            throw new TimeoutException("ZLink async submit timed out before the socket became writable.");
        }

        var pending = new PendingSubmit(
            message,
            trySubmit,
            deadline,
            Drain,
            new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously),
            completeOnAccepted: true);
        pending.StartDeadlineTimer();
        CancellationTokenRegistration cancellationRegistration = default;
        if (cancellationToken.CanBeCanceled || _stopToken.CanBeCanceled)
        {
            var linkedSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _stopToken);
            cancellationRegistration = linkedSource.Token.Register(static state =>
            {
                var item = (PendingSubmit)state!;
                item.TryCancel();
            }, pending);
            pending.SetCancellation(linkedSource, cancellationRegistration);
        }

        lock (_gate)
        {
            ThrowIfDisposed();
            if (_pending.Count >= _capacity)
            {
                pending.Dispose();
                throw new InvalidOperationException("ZLink async submit queue is full.");
            }

            _pending.Enqueue(pending);
        }

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

        var deadline = ResolveDeadline();
        if (deadline is DateTimeOffset nowDeadline && nowDeadline <= DateTimeOffset.UtcNow)
        {
            message.Dispose();
            throw new TimeoutException("ZLink async submit timed out before the socket became writable.");
        }

        var pendingSubmit = new PendingSubmit(
            message,
            Submit,
            deadline,
            Drain,
            completion,
            completeOnAccepted: false);
        pendingSubmit.StartDeadlineTimer();
        if (cancellationToken.CanBeCanceled || _stopToken.CanBeCanceled)
        {
            var linkedSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, _stopToken);
            var cancellationRegistration = linkedSource.Token.Register(static state =>
            {
                var item = (PendingSubmit)state!;
                item.TryCancel();
            }, pendingSubmit);
            pendingSubmit.SetCancellation(linkedSource, cancellationRegistration);
        }

        lock (_gate)
        {
            ThrowIfDisposed();
            if (_pending.Count >= _capacity)
            {
                pendingSubmit.Dispose();
                throw new InvalidOperationException("ZLink async submit queue is full.");
            }

            _pending.Enqueue(pendingSubmit);
        }

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

                if (!TrySubmitNow(item.Message, item.TrySubmit))
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
                Result: SubmitResult.Backpressured or SubmitResult.NotConnected
            })
        {
            return true;
        }

        return ZlinkException.MapErrorCode(error.InternalErrno) is
            ErrorCode.EAgain or
            ErrorCode.ENotConn or
            ErrorCode.EHostUnreach or
            ErrorCode.ETimedOut;
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

    private sealed class PendingSubmit(
        Message message,
        Func<Message, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        TaskCompletionSource<object?> completion,
        bool completeOnAccepted) : IDisposable
    {
        private System.Threading.Timer? _deadlineTimer;
        private CancellationTokenSource? _linkedCancellation;
        private CancellationTokenRegistration _cancellationRegistration;
        private int _completed;

        public Message Message { get; } = message;

        public Func<Message, bool> TrySubmit { get; } = trySubmit;

        public DateTimeOffset? Deadline { get; } = deadline;

        public bool CompleteOnAccepted { get; } = completeOnAccepted;

        public Task<object?> Task => completion.Task;

        public bool IsCompleted => Volatile.Read(ref _completed) != 0;

        public void StartDeadlineTimer()
        {
            if (Deadline is not { } deadline)
            {
                return;
            }

            var dueTime = deadline - DateTimeOffset.UtcNow;
            if (dueTime < TimeSpan.Zero)
            {
                dueTime = TimeSpan.Zero;
            }

            _deadlineTimer = new System.Threading.Timer(static state =>
            {
                var item = (PendingSubmit)state!;
                item.TryFail(new TimeoutException("ZLink async submit timed out before the socket became writable."));
            }, this, dueTime, Timeout.InfiniteTimeSpan);
        }

        public void SetCancellation(
            CancellationTokenSource linkedCancellation,
            CancellationTokenRegistration cancellationRegistration)
        {
            _linkedCancellation = linkedCancellation;
            _cancellationRegistration = cancellationRegistration;
        }

        public void TryComplete(object? result)
        {
            if (Interlocked.Exchange(ref _completed, 1) == 0)
            {
                completion.TrySetResult(result);
            }
        }

        public void TryCancel()
        {
            if (Interlocked.Exchange(ref _completed, 1) == 0)
            {
                completion.TrySetCanceled();
                wake();
            }
        }

        public void TryFail(Exception exception)
        {
            if (Interlocked.Exchange(ref _completed, 1) == 0)
            {
                completion.TrySetException(exception);
                wake();
            }
        }

        public void Dispose()
        {
            _deadlineTimer?.Dispose();
            _cancellationRegistration.Dispose();
            _linkedCancellation?.Dispose();
            Message.Dispose();
        }
    }
}
