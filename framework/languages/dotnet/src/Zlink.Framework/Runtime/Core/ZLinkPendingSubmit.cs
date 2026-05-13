namespace Zlink.Framework.Runtime.Core;

internal sealed class PendingSubmit : IDisposable
{
    private readonly Action _wake;
    private readonly TaskCompletionSource<object?> _completion;
    private System.Threading.Timer? _deadlineTimer;
    private CancellationTokenSource? _linkedCancellation;
    private CancellationTokenRegistration _cancellationRegistration;
    private int _completed;

    private PendingSubmit(
        Message message,
        Func<Message, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        TaskCompletionSource<object?> completion,
        bool completeOnAccepted)
    {
        Message = message;
        TrySubmit = trySubmit;
        Deadline = deadline;
        _wake = wake;
        _completion = completion;
        CompleteOnAccepted = completeOnAccepted;
    }

    public static PendingSubmit CreateCommand(
        Message message,
        Func<Message, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake)
    {
        return new PendingSubmit(
            message,
            trySubmit,
            deadline,
            wake,
            new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously),
            completeOnAccepted: true);
    }

    public static PendingSubmit CreateRequest(
        Message message,
        Func<Message, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        TaskCompletionSource<object?> completion)
    {
        return new PendingSubmit(
            message,
            trySubmit,
            deadline,
            wake,
            completion,
            completeOnAccepted: false);
    }

    public Message Message { get; }

    public Func<Message, bool> TrySubmit { get; }

    public DateTimeOffset? Deadline { get; }

    public bool CompleteOnAccepted { get; }

    public Task<object?> Task => _completion.Task;

    public bool IsCompleted => Volatile.Read(ref _completed) != 0;

    public void Activate(CancellationToken cancellationToken, CancellationToken stopToken)
    {
        StartDeadlineTimer();
        RegisterCancellation(cancellationToken, stopToken);
    }

    public void TryComplete(object? result)
    {
        if (Interlocked.Exchange(ref _completed, 1) == 0)
        {
            _completion.TrySetResult(result);
        }
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

    public void Dispose()
    {
        _deadlineTimer?.Dispose();
        _cancellationRegistration.Dispose();
        _linkedCancellation?.Dispose();
        Message.Dispose();
    }

    private void StartDeadlineTimer()
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

    private void RegisterCancellation(CancellationToken cancellationToken, CancellationToken stopToken)
    {
        var hasCallerCancellation = cancellationToken.CanBeCanceled;
        var hasStopCancellation = stopToken.CanBeCanceled;
        if (!hasCallerCancellation && !hasStopCancellation)
        {
            return;
        }

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
