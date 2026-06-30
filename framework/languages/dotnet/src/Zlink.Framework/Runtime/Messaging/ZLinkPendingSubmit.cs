namespace Zlink.Framework.Runtime.Messaging;

internal sealed class PendingSubmit : IDisposable
{
    private readonly TaskCompletionSource<object?> _completion;
    private readonly Action _wake;
    private CancellationTokenRegistration _cancellationRegistration;
    private int _completed;
    private Timer? _deadlineTimer;
    private Exception? _lastSubmitFailure;
    private CancellationTokenSource? _linkedCancellation;

    private PendingSubmit(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        TaskCompletionSource<object?> completion,
        bool completeOnAccepted)
    {
        Parts = parts;
        TrySubmit = trySubmit;
        Deadline = deadline;
        _wake = wake;
        _completion = completion;
        CompleteOnAccepted = completeOnAccepted;
    }

    public IReadOnlyList<Message> Parts { get; }

    public Func<IReadOnlyList<Message>, bool> TrySubmit { get; }

    public DateTimeOffset? Deadline { get; }

    public bool CompleteOnAccepted { get; }

    public Task<object?> Task => _completion.Task;

    public bool IsCompleted => Volatile.Read(ref _completed) != 0;

    public void Dispose()
    {
        _deadlineTimer?.Dispose();
        _cancellationRegistration.Dispose();
        _linkedCancellation?.Dispose();
        foreach (var part in Parts) part.Dispose();
    }

    public static PendingSubmit CreateCommand(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake)
    {
        return new PendingSubmit(
            parts,
            trySubmit,
            deadline,
            wake,
            new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously),
            true);
    }

    public static PendingSubmit CreateRequest(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        TaskCompletionSource<object?> completion)
    {
        return new PendingSubmit(
            parts,
            trySubmit,
            deadline,
            wake,
            completion,
            false);
    }

    public void Activate(CancellationToken cancellationToken, CancellationToken stopToken)
    {
        StartDeadlineTimer();
        RegisterCancellation(cancellationToken, stopToken);
    }

    public void TryComplete(object? result)
    {
        if (Interlocked.Exchange(ref _completed, 1) == 0) _completion.TrySetResult(result);
    }

    public void TryCancel()
    {
        if (Interlocked.Exchange(ref _completed, 1) == 0)
        {
            _completion.TrySetCanceled();
            _wake();
        }
    }

    public void TryFail(Exception exception)
    {
        if (Interlocked.Exchange(ref _completed, 1) == 0)
        {
            _completion.TrySetException(exception);
            _wake();
        }
    }

    public void RecordSubmitFailure(Exception exception)
    {
        Volatile.Write(ref _lastSubmitFailure, exception);
    }

    private void StartDeadlineTimer()
    {
        if (Deadline is not { } deadline) return;

        var dueTime = deadline - DateTimeOffset.UtcNow;
        if (dueTime < TimeSpan.Zero) dueTime = TimeSpan.Zero;

        _deadlineTimer = new Timer(static state =>
        {
            var item = (PendingSubmit)state!;
            item.TryFail(item.CreateDeadlineException());
        }, this, dueTime, Timeout.InfiniteTimeSpan);
    }

    private Exception CreateDeadlineException()
    {
        var lastSubmitFailure = Volatile.Read(ref _lastSubmitFailure);
        return CompleteOnAccepted
            ? CreateCommandDeadlineException(lastSubmitFailure)
            : ZLinkRequestFailureMapper.CreateSubmitTimeoutException(
                lastSubmitFailure,
                "ZLink request submit");
    }

    private static Exception CreateCommandDeadlineException(Exception? lastSubmitFailure)
    {
        return lastSubmitFailure is null
            ? new TimeoutException("ZLink async submit timed out before the socket became writable.")
            : new TimeoutException(
                "ZLink async submit timed out before the socket became writable.",
                lastSubmitFailure);
    }

    private void RegisterCancellation(CancellationToken cancellationToken, CancellationToken stopToken)
    {
        var hasCallerCancellation = cancellationToken.CanBeCanceled;
        var hasStopCancellation = stopToken.CanBeCanceled;
        if (!hasCallerCancellation && !hasStopCancellation) return;

        if (hasCallerCancellation && hasStopCancellation)
        {
            _linkedCancellation = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                stopToken);
            RegisterCancellation(_linkedCancellation.Token);
            return;
        }

        RegisterCancellation(hasCallerCancellation ? cancellationToken : stopToken);
    }

    private void RegisterCancellation(CancellationToken token)
    {
        if (token.IsCancellationRequested)
        {
            TryCancel();
            return;
        }

        _cancellationRegistration = token.Register(static state =>
        {
            var item = (PendingSubmit)state!;
            item.TryCancel();
        }, this);
    }
}