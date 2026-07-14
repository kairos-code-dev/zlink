namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSerialTurn
{
    private static readonly AsyncLocal<ZLinkSerialTurn?> CurrentTurn = new();

    private readonly Func<ZLinkSerialTurn, Action, bool> _postResume;
    private readonly CancellationToken _executionToken;
    private Task? _ownerTask;

    private TaskCompletionSource _suspended =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private int _suspendSignaled;

    public ZLinkSerialTurn(
        Func<ZLinkSerialTurn, Action, bool> postResume,
        CancellationToken executionToken)
    {
        _postResume = postResume;
        _executionToken = executionToken;
    }

    public static ZLinkSerialTurn? Current => CurrentTurn.Value;

    public Task Suspended => Volatile.Read(ref _suspended).Task;

    public Task? OwnerTask => Volatile.Read(ref _ownerTask);

    public void BindOwnerTask(Task ownerTask)
    {
        Volatile.Write(ref _ownerTask, ownerTask);
    }

    public void ResetSuspension()
    {
        Volatile.Write(
            ref _suspended,
            new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        Volatile.Write(ref _suspendSignaled, 0);
    }

    public static Scope Push(ZLinkSerialTurn turn)
    {
        var previous = CurrentTurn.Value;
        CurrentTurn.Value = turn;
        return new Scope(previous);
    }

    public static Scope Suppress()
    {
        var previous = CurrentTurn.Value;
        CurrentTurn.Value = null;
        return new Scope(previous);
    }

    public async ValueTask<T> YieldFrameworkCallAsync<T>(
        Func<CancellationToken, ValueTask<T>> submit,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var operation = submit(cancellationToken);
        if (operation.IsCompletedSuccessfully) return operation.Result;

        SignalSuspended();
        try
        {
            return await operation.AsTask().WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            // Execution cancellation terminates this turn. Posting a resume
            // after the queue has begun completion races the closed writer
            // and adds a scheduler hop before callback cleanup can run.
            // The owning work item still joins the callback task, so cleanup
            // remains part of queue disposal without re-entering a dead turn.
            if (!_executionToken.IsCancellationRequested)
                await AwaitResumePermitAsync().ConfigureAwait(false);
        }
    }

    public async ValueTask YieldFrameworkCallAsync(
        Func<CancellationToken, ValueTask> submit,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var operation = submit(cancellationToken);
        if (operation.IsCompletedSuccessfully) return;

        SignalSuspended();
        try
        {
            await operation.AsTask().WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            if (!_executionToken.IsCancellationRequested)
                await AwaitResumePermitAsync().ConfigureAwait(false);
        }
    }

    private void SignalSuspended()
    {
        if (Interlocked.Exchange(ref _suspendSignaled, 1) == 0) Volatile.Read(ref _suspended).TrySetResult();
    }

    private Task AwaitResumePermitAsync()
    {
        var resume = new TaskCompletionSource();
        if (!_postResume(this, () => resume.TrySetResult()))
            resume.TrySetException(
                new ObjectDisposedException(
                    nameof(ZLinkSerialExecutionQueue),
                    "ZLink serial execution queue is closed."));

        return resume.Task;
    }

    public readonly struct Scope : IDisposable
    {
        private readonly ZLinkSerialTurn? _previous;

        internal Scope(ZLinkSerialTurn? previous)
        {
            _previous = previous;
        }

        public void Dispose()
        {
            CurrentTurn.Value = _previous;
        }
    }
}
