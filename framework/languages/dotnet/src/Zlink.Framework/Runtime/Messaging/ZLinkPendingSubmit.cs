namespace Zlink.Framework.Runtime.Messaging;

internal sealed class PendingSubmit : IDisposable
{
    private readonly IPendingSubmitCompletion _completion;
    private readonly Action _wake;
    private CancellationTokenRegistration _callerCancellationRegistration;
    private int _completed;
    private Timer? _deadlineTimer;
    private Exception? _lastSubmitFailure;
    private CancellationTokenRegistration _stopCancellationRegistration;

    private PendingSubmit(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        IPendingSubmitCompletion completion,
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

    public Task Task => _completion.Task;

    public bool IsCompleted => Volatile.Read(ref _completed) != 0 || _completion.Task.IsCompleted;

    public void Dispose()
    {
        _deadlineTimer?.Dispose();
        _callerCancellationRegistration.Dispose();
        _stopCancellationRegistration.Dispose();
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
            new ObjectPendingSubmitCompletion(
                new TaskCompletionSource<object?>(TaskCreationOptions.RunContinuationsAsynchronously)),
            true);
    }

    public static PendingSubmit CreateRequest<T>(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        DateTimeOffset? deadline,
        Action wake,
        ZLinkRequestCompletion<T> completion)
    {
        return new PendingSubmit(
            parts,
            trySubmit,
            deadline,
            wake,
            new RequestPendingSubmitCompletion<T>(completion),
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

    public void TryCancel(CancellationToken cancellationToken)
    {
        if (Interlocked.Exchange(ref _completed, 1) == 0)
        {
            _completion.TrySetCanceled(cancellationToken);
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
        _callerCancellationRegistration = RegisterCancellation(cancellationToken);
        _stopCancellationRegistration = RegisterCancellation(stopToken);
    }

    private CancellationTokenRegistration RegisterCancellation(CancellationToken token)
    {
        if (token.IsCancellationRequested)
        {
            TryCancel(token);
            return default;
        }

        return token.Register(static state =>
        {
            var cancellation = (CancellationState)state!;
            cancellation.Submit.TryCancel(cancellation.Token);
        }, new CancellationState(this, token));
    }

    private interface IPendingSubmitCompletion
    {
        Task Task { get; }

        void TrySetResult(object? result);

        void TrySetCanceled(CancellationToken cancellationToken);

        void TrySetException(Exception exception);
    }

    private sealed class ObjectPendingSubmitCompletion(TaskCompletionSource<object?> source)
        : IPendingSubmitCompletion
    {
        public Task Task => source.Task;

        public void TrySetResult(object? result)
        {
            source.TrySetResult(result);
        }

        public void TrySetCanceled(CancellationToken cancellationToken)
        {
            source.TrySetCanceled(cancellationToken);
        }

        public void TrySetException(Exception exception)
        {
            source.TrySetException(exception);
        }
    }

    private sealed class RequestPendingSubmitCompletion<T>(ZLinkRequestCompletion<T> completion)
        : IPendingSubmitCompletion
    {
        public Task Task => completion.Task;

        public void TrySetResult(object? result)
        {
            throw new InvalidOperationException("Request submissions complete only from their native callback.");
        }

        public void TrySetCanceled(CancellationToken cancellationToken)
        {
            completion.Cancel(cancellationToken);
        }

        public void TrySetException(Exception exception)
        {
            completion.Fail(exception);
        }
    }

    private sealed record CancellationState(PendingSubmit Submit, CancellationToken Token);
}
