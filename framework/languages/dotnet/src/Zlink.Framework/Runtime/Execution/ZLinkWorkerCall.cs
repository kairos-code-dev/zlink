namespace Zlink.Framework.Runtime.Execution;

/// <summary>
/// Fluent worker offload call. The work delegate runs on a pool thread; the
/// terminator decides how completion is observed. <c>Async()</c> is the gated
/// awaitable path; <c>Submit(...)</c> always posts completion and error
/// callbacks back to the owning spot serial line. A late completion after a
/// timeout is dropped without invoking user callbacks.
/// </summary>
internal sealed class ZLinkWorkerCall<TResult>(
    ZLinkWorkerPool pool,
    Func<CancellationToken, TResult> work,
    Action<Func<CancellationToken, ValueTask>> postToDispatcher) : IZLinkWorkerCall<TResult>
{
    private TimeSpan? _timeout;
    private int _terminated;

    public IZLinkWorkerCall<TResult> Timeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        _timeout = timeout;
        return this;
    }

    public ValueTask<TResult> Async(CancellationToken cancellationToken = default)
    {
        EnsureSingleTerminator();
        var completion = new TaskCompletionSource<TResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        Start(
            result => completion.TrySetResult(result),
            error => completion.TrySetException(error),
            cancellationToken);
        return new ValueTask<TResult>(completion.Task);
    }

    public void Submit(
        Func<TResult, CancellationToken, ValueTask> onCompleted,
        Func<Exception, CancellationToken, ValueTask>? onError = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(onCompleted);
        EnsureSingleTerminator();
        Start(
            result => postToDispatcher(ct => onCompleted(result, ct)),
            error => postToDispatcher(ct =>
            {
                if (onError is null)
                {
                    // Surfaces through the serial line's runtime error sink.
                    throw error;
                }

                return onError(error, ct);
            }),
            cancellationToken);
    }

    private void Start(
        Action<TResult> complete,
        Action<Exception> fail,
        CancellationToken callerToken)
    {
        var execution = new Execution(work, complete, fail, _timeout);
        if (!execution.TryBind(pool, callerToken))
        {
            return;
        }

        if (!pool.TrySubmit(execution.Run))
        {
            execution.FailQueueFull();
        }
    }

    private void EnsureSingleTerminator()
    {
        if (Interlocked.Exchange(ref _terminated, 1) != 0)
        {
            throw new InvalidOperationException(
                "RunWorker call already has a terminator. Call Async or Submit once.");
        }
    }

    private sealed class Execution(
        Func<CancellationToken, TResult> work,
        Action<TResult> complete,
        Action<Exception> fail,
        TimeSpan? timeout)
    {
        private readonly Action<TResult> _complete = complete;
        private readonly Action<Exception> _fail = fail;
        private CancellationTokenSource? _timeoutSource;
        private CancellationTokenSource? _workTokenSource;
        private CancellationTokenRegistration _timeoutRegistration;
        private CancellationTokenRegistration _callerRegistration;
        private int _settled;

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
            {
                _callerRegistration = callerToken.Register(
                    () => TrySettle(static (self, _) => self._fail(
                            new OperationCanceledException("Worker call was canceled.")),
                        this));
            }

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
                            isRetriable: false,
                            innerException: (Exception)state!)),
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
                        isRetriable: true)),
                this);
            Cleanup();
        }

        private void FailTimedOut()
        {
            TrySettle(static (self, _) => self._fail(
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.WorkerTimedOut,
                        "Worker call timed out.",
                        isRetriable: false)),
                this);
        }

        private void TrySettle(
            Action<Execution, object?> settle,
            Execution self,
            object? state = null)
        {
            if (Interlocked.Exchange(ref _settled, 1) != 0)
            {
                // Late completion after timeout/cancellation: drop the result.
                return;
            }

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
