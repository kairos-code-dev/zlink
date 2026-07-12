using System.Collections;

namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkAsyncSubmitter : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;

    private readonly object _gate = new();
    private readonly object _disposeGate = new();
    private readonly ZLinkSubmitOperationFactory _operationFactory;
    private readonly ZLinkSubmitQueue _pending;
    private readonly TimeSpan? _sendTimeout;
    private readonly CancellationToken _stopToken;
    private readonly object _submitGate = new();
    private readonly Func<bool>? _failFastNotConnected;
    private TaskCompletionSource? _activeDrainCompletion;
    private Task? _disposeTask;
    private bool _draining;
    private bool _accepting = true;

    /// <summary>
    /// <paramref name="failFastNotConnected"/>: when it returns true,
    /// NotConnected submit failures fail immediately instead of retrying
    /// until the writability timeout. Rid-addressed router paths managed by
    /// auto-connect opt in so a stale or unconverged target surfaces as a
    /// typed error the caller can act on (spot-address messaging contract §7);
    /// dealer and pub sockets keep the connect-window buffering.
    /// </summary>
    public ZLinkAsyncSubmitter(
        Action<Action> registerReadyHandler,
        TimeSpan? sendTimeout,
        CancellationToken stopToken,
        int capacity = DefaultCapacity,
        Func<bool>? failFastNotConnected = null)
    {
        _failFastNotConnected = failFastNotConnected;
        _pending = new ZLinkSubmitQueue(capacity);
        _sendTimeout = ValidateTimeout(sendTimeout);
        _stopToken = stopToken;
        _operationFactory = new ZLinkSubmitOperationFactory(_sendTimeout, Drain);
        registerReadyHandler(OnSendReady);
    }

    public ValueTask DisposeAsync()
    {
        Task disposeTask;
        TaskCompletionSource? startDispose = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                startDispose = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeAfterStartAsync(startDispose.Task);
            }

            disposeTask = _disposeTask;
        }

        startDispose?.TrySetResult();
        return new ValueTask(disposeTask);
    }

    private async Task DisposeAfterStartAsync(Task started)
    {
        await started.ConfigureAwait(false);

        Task activeDrain;
        lock (_submitGate)
        {
            Volatile.Write(ref _accepting, false);
            _pending.Complete();
        }

        lock (_gate)
            activeDrain = _activeDrainCompletion?.Task ?? Task.CompletedTask;

        await activeDrain.ConfigureAwait(false);

        // Entering this gate after the drain completes also joins an immediate
        // submission that began before admission was closed.
        lock (_submitGate)
        {
        }

        var remaining = _pending.DrainAll();

        foreach (var item in remaining)
            try
            {
                item.TryFail(new ObjectDisposedException(nameof(ZLinkAsyncSubmitter)));
            }
            finally
            {
                item.Dispose();
            }

    }

    public ValueTask Async(
        Message message,
        Func<Message, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(trySubmit);

        return SubmitCommandAsync(
            new SingleMessageParts(message),
            parts => trySubmit(parts[0]),
            cancellationToken);
    }

    public ValueTask Async(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(parts);
        ArgumentNullException.ThrowIfNull(trySubmit);
        EnsureNotEmpty(parts);

        return SubmitCommandAsync(parts, trySubmit, cancellationToken);
    }

    public ValueTask<T> SubmitRequestAsync<T>(
        Message message,
        Func<Message, Action<T>, Action<Exception>, bool> trySubmit,
        CancellationToken cancellationToken = default,
        Action<T>? discardResult = null)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(trySubmit);

        return SubmitRequestCoreAsync<T>(
            new SingleMessageParts(message),
            (parts, onResult, onError) => trySubmit(parts[0], onResult, onError),
            cancellationToken,
            discardResult);
    }

    public ValueTask<T> SubmitRequestAsync<T>(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, Action<T>, Action<Exception>, bool> trySubmit,
        CancellationToken cancellationToken = default,
        Action<T>? discardResult = null)
    {
        ArgumentNullException.ThrowIfNull(parts);
        ArgumentNullException.ThrowIfNull(trySubmit);
        EnsureNotEmpty(parts);

        return SubmitRequestCoreAsync(parts, trySubmit, cancellationToken, discardResult);
    }

    private ValueTask SubmitCommandAsync(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        CancellationToken cancellationToken)
    {
        var ownsParts = true;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            _stopToken.ThrowIfCancellationRequested();

            if (TrySubmitNow(parts, trySubmit, out var submitFailure))
            {
                ownsParts = false;
                ZLinkMessageParts.DisposeAll(parts);
                return ValueTask.CompletedTask;
            }

            if (submitFailure is ZlinkSubmitException submitError && !IsRetryableSubmitFailure(submitError))
            {
                ownsParts = false;
                ZLinkMessageParts.DisposeAll(parts);
                throw ZLinkRequestFailureMapper.CreateSubmitException(
                    submitError,
                    "ZLink command submit");
            }

            var pending = _operationFactory.CreateCommand(parts, trySubmit);
            if (submitFailure is not null) pending.RecordSubmitFailure(submitFailure);

            ownsParts = false;
            EnqueuePending(pending, cancellationToken);
            return new ValueTask(pending.Task);
        }
        catch
        {
            if (ownsParts) ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    private async ValueTask<T> SubmitRequestCoreAsync<T>(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, Action<T>, Action<Exception>, bool> trySubmit,
        CancellationToken cancellationToken,
        Action<T>? discardResult)
    {
        var ownsParts = true;
        using var completion = new ZLinkRequestCompletion<T>(
            cancellationToken,
            _stopToken,
            discardResult,
            Drain);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            _stopToken.ThrowIfCancellationRequested();

            bool Submit(IReadOnlyList<Message> pending)
            {
                return trySubmit(
                    pending,
                    completion.Complete,
                    completion.Fail);
            }

            if (TrySubmitNow(parts, Submit, out var retryableFailure))
            {
                ownsParts = false;
                ZLinkMessageParts.DisposeAll(parts);
                return await completion.Task.ConfigureAwait(false);
            }

            if (retryableFailure is ZlinkSubmitException submitError
                && !IsRetryableSubmitFailure(submitError))
            {
                ownsParts = false;
                ZLinkMessageParts.DisposeAll(parts);
                completion.Fail(ZLinkRequestFailureMapper.CreateSubmitException(
                    submitError,
                    "ZLink request submit"));
                return await completion.Task.ConfigureAwait(false);
            }

            var pendingSubmit = _operationFactory.CreateRequest(parts, Submit, completion);
            if (retryableFailure is not null) pendingSubmit.RecordSubmitFailure(retryableFailure);

            ownsParts = false;
            EnqueuePending(pendingSubmit, cancellationToken);
            return await completion.Task.ConfigureAwait(false);
        }
        catch
        {
            if (ownsParts) ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
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
        TaskCompletionSource drainCompletion;
        lock (_gate)
        {
            if (_draining || !Volatile.Read(ref _accepting)) return;

            _draining = true;
            drainCompletion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _activeDrainCompletion = drainCompletion;
        }

        try
        {
            while (true)
            {
                PendingSubmit? item;
                _pending.TryPeek(out item);
                if (item is null) return;

                if (item.IsCompleted)
                {
                    Dequeue(item);
                    continue;
                }

                if (item.Deadline is DateTimeOffset deadline && deadline <= DateTimeOffset.UtcNow)
                {
                    item.TryFail(
                        new TimeoutException("ZLink async submit timed out before the socket became writable."));
                    Dequeue(item);
                    continue;
                }

                if (!TrySubmitNow(item.Parts, item.TrySubmit, out var retryableFailure))
                {
                    if (retryableFailure is not null)
                    {
                        if (retryableFailure is ZlinkSubmitException submitError
                            && !IsRetryableSubmitFailure(submitError))
                        {
                            item.TryFail(ZLinkRequestFailureMapper.CreateSubmitException(
                                submitError,
                                item.CompleteOnAccepted ? "ZLink command submit" : "ZLink request submit"));
                            Dequeue(item);
                            continue;
                        }

                        item.RecordSubmitFailure(retryableFailure);
                    }

                    return;
                }

                if (item.CompleteOnAccepted) item.TryComplete(null);

                Dequeue(item);
            }
        }
        finally
        {
            lock (_gate)
            {
                _draining = false;
                if (ReferenceEquals(_activeDrainCompletion, drainCompletion))
                    _activeDrainCompletion = null;
            }
            drainCompletion.TrySetResult();
        }
    }

    private bool TrySubmitNow(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        out ZlinkException? retryableFailure)
    {
        lock (_submitGate)
        {
            ObjectDisposedException.ThrowIf(!_accepting, this);
            retryableFailure = null;
            IReadOnlyList<Message>? attempt = null;
            try
            {
                attempt = ZLinkMessageParts.CopyAll(parts);
                return trySubmit(attempt);
            }
            catch (ZlinkException error)
            {
                retryableFailure = error;
                return false;
            }
            finally
            {
                if (attempt is not null) ZLinkMessageParts.DisposeAll(attempt);
            }
        }
    }

    private bool IsRetryableSubmitFailure(ZlinkException error)
    {
        if (error is not ZlinkSubmitException submitError) return false;

        if (submitError.Result == ZlinkSubmitException.ErrorCode.Backpressured) return true;

        return submitError.Result == ZlinkSubmitException.ErrorCode.NotConnected
               && _failFastNotConnected?.Invoke() != true;
    }

    private void Dequeue(PendingSubmit expected)
    {
        if (_pending.TryDequeue(expected, out var pending) && pending is not null) pending.Dispose();
    }

    private static TimeSpan? ValidateTimeout(TimeSpan? timeout)
    {
        if (timeout is { } value && value < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(timeout),
                "SendTimeout must be null, zero, or a positive duration.");

        return timeout;
    }

    private static void EnsureNotEmpty(IReadOnlyList<Message> parts)
    {
        if (parts.Count == 0) throw new ArgumentException("At least one message part is required.", nameof(parts));
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

        IEnumerator IEnumerable.GetEnumerator()
        {
            return GetEnumerator();
        }
    }
}
