namespace Zlink.Framework.Runtime.Execution;

/// <summary>
///     Fluent worker offload call. The work delegate runs on a pool thread. The
///     <c>Async()</c> keeps the current framework turn, while <c>Yield()</c>
///     releases it and resumes through the serial queue. A late completion
///     after a timeout is dropped.
/// </summary>
internal sealed class ZLinkWorkerCall<TResult>(
    ZLinkWorkerPool pool,
    Func<CancellationToken, TResult> work) : IZLinkWorkerCall<TResult>
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private int _terminated;
    private TimeSpan? _timeout;

    public IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));

        _timeout = timeout;
        return this;
    }

    public ValueTask<TResult> Async(CancellationToken cancellationToken = default)
    {
        EnsureSingleTerminator();
        return ExecuteAsync(cancellationToken);
    }

    public ValueTask<TResult> Yield(CancellationToken cancellationToken = default)
    {
        EnsureSingleTerminator();
        return _turn is null
            ? ExecuteAsync(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync, cancellationToken);
    }

    private ValueTask<TResult> ExecuteAsync(CancellationToken cancellationToken)
    {
        var completion = new TaskCompletionSource<TResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        Start(
            result => completion.TrySetResult(result),
            error => completion.TrySetException(error),
            cancellationToken);
        return new ValueTask<TResult>(completion.Task);
    }

    private void Start(
        Action<TResult> complete,
        Action<Exception> fail,
        CancellationToken callerToken)
    {
        var execution = new Execution(work, complete, fail, _timeout);
        if (!execution.TryBind(pool, callerToken)) return;

        switch (pool.TrySubmit(execution.Run, execution.FailStopped))
        {
            case ZLinkWorkerSubmitResult.Accepted:
                break;
            case ZLinkWorkerSubmitResult.Full:
                execution.FailQueueFull();
                break;
            case ZLinkWorkerSubmitResult.Stopped:
                execution.FailStopped();
                break;
            default:
                throw new ArgumentOutOfRangeException();
        }
    }

    private void EnsureSingleTerminator()
    {
        if (Interlocked.Exchange(ref _terminated, 1) != 0)
            throw new InvalidOperationException(
                "RunWorker call already has a terminator. Call Async or Yield once.");
    }

    private sealed class Execution(
        Func<CancellationToken, TResult> work,
        Action<TResult> complete,
        Action<Exception> fail,
        TimeSpan? timeout)
    {
        private readonly Action<TResult> _complete = complete;
        private readonly Action<Exception> _fail = fail;
        private CancellationTokenRegistration _callerRegistration;
        private int _settled;
        private CancellationTokenRegistration _timeoutRegistration;
        private CancellationTokenSource? _timeoutSource;
        private CancellationTokenSource? _workTokenSource;

        public bool TryBind(ZLinkWorkerPool pool, CancellationToken callerToken)
        {
            _workTokenSource = CancellationTokenSource.CreateLinkedTokenSource(
                pool.ShutdownToken,
                callerToken);
            if (timeout is { } timeoutValue)
            {
                _timeoutSource = new CancellationTokenSource(timeoutValue);
                _timeoutRegistration = _timeoutSource.Token.Register(FailTimedOut);
            }

            if (callerToken.CanBeCanceled)
                _callerRegistration = callerToken.Register(() => TrySettle(static (self, _) => self._fail(
                        new OperationCanceledException("Worker call was canceled.")),
                    this));

            return true;
        }

        public void Run(CancellationToken shutdownToken)
        {
            _ = shutdownToken;
            try
            {
                using var linked = _timeoutSource is null
                    ? null
                    : CancellationTokenSource.CreateLinkedTokenSource(
                        _workTokenSource!.Token,
                        _timeoutSource.Token);
                var result = work(linked?.Token ?? _workTokenSource!.Token);
                TrySettle(static (self, state) => self._complete((TResult)state!), this, result);
            }
            catch (Exception ex)
            {
                TrySettle(static (self, state) => self._fail(
                        new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.WorkerFailed,
                            "Worker call failed.",
                            false,
                            (Exception)state!)),
                    this,
                    ex);
            }
            finally
            {
                Cleanup();
            }
        }

        public void FailQueueFull()
        {
            TrySettle(static (self, _) => self._fail(
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.WorkerQueueFull,
                        "Worker queue is full.",
                        true)),
                this);
            Cleanup();
        }

        public void FailStopped()
        {
            TrySettle(static (self, _) => self._fail(
                    new OperationCanceledException(
                        "Worker call was canceled because the framework runtime stopped.")),
                this);
            Cleanup();
        }

        private void FailTimedOut()
        {
            TrySettle(static (self, _) => self._fail(
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.WorkerTimedOut,
                        "Worker call timed out.",
                        false)),
                this);
        }

        private void TrySettle(
            Action<Execution, object?> settle,
            Execution self,
            object? state = null)
        {
            if (Interlocked.Exchange(ref _settled, 1) != 0)
                // Late completion after timeout/cancellation: drop the result.
                return;

            settle(self, state);
        }

        private void Cleanup()
        {
            _timeoutRegistration.Dispose();
            _callerRegistration.Dispose();
            _timeoutSource?.Dispose();
            _workTokenSource?.Dispose();
        }
    }
}
