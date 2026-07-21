using System.Collections;
using Zlink.Framework.Runtime.Configuration;
using Zlink.Framework.Runtime.Diagnostics;

namespace Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkAsyncSubmitter : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;

    private readonly object _gate = new();
    private readonly object _disposeGate = new();
    private readonly ZLinkSubmitOperationFactory _operationFactory;
    private readonly ZLinkSubmitQueue _pending;
    private readonly TimeSpan _sendTimeout;
    private readonly CancellationToken _stopToken;
    private readonly object _submitGate = new();
    private readonly Func<bool>? _failFastNotConnected;
    private TaskCompletionSource? _activeDrainCompletion;
    private Task? _disposeTask;
    private bool _draining;
    private bool _accepting = true;
    private int _initialAttempts;
    private long _readyCredits;

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
        _operationFactory = new ZLinkSubmitOperationFactory(_sendTimeout, _submitGate, OnPendingTerminal);
        registerReadyHandler(OnSendReady);
    }

    internal static int ResolvePendingCapacity(int sendHighWaterMark) =>
        sendHighWaterMark > 0 ? sendHighWaterMark : DefaultCapacity;

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
                Trace(item.OperationId, "cleanup");
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

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> attemptSubmit,
        CancellationToken cancellationToken = default)
    {
        try
        {
            await Async(parts, attemptSubmit, cancellationToken).ConfigureAwait(false);
            return new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted);
        }
        catch (ZLinkPendingAdmissionFullException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.Backpressured);
        }
        catch (TimeoutException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TimedOut);
        }
        catch (ZLinkSubmitShutdownException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.Shutdown);
        }
        catch (ObjectDisposedException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.Shutdown);
        }
        catch (ZLinkFrameworkException failure)
            when (ZLinkMeshCallSupport.TryMapSubmitFailure(failure, out var result))
        {
            return result;
        }
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
        var operationId = ZLinkTelemetry.CaptureSubmitOperationId();
        var ownsParts = true;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_stopToken.IsCancellationRequested)
                throw new ZLinkSubmitShutdownException();

            BeginInitialAttempt();
            try
            {
                Trace(operationId, "transport-attempt");
                if (TrySubmitNow(parts, trySubmit, out var submitFailure))
                {
                    Trace(operationId, "commit");
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

                var pending = _operationFactory.CreateCommand(parts, trySubmit, operationId);
                if (submitFailure is not null) pending.RecordSubmitFailure(submitFailure);

                ownsParts = false;
                EnqueuePending(pending, cancellationToken);
                return new ValueTask(pending.Task);
            }
            finally
            {
                EndInitialAttempt();
            }
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
        var operationId = ZLinkTelemetry.CaptureSubmitOperationId();
        var ownsParts = true;
        using var completion = new ZLinkRequestCompletion<T>(
            cancellationToken,
            _stopToken,
            discardResult,
            Drain);
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (_stopToken.IsCancellationRequested)
                throw new ZLinkSubmitShutdownException();

            bool Submit(IReadOnlyList<Message> pending)
            {
                return trySubmit(
                    pending,
                    completion.Complete,
                    completion.Fail);
            }

            BeginInitialAttempt();
            try
            {
                Trace(operationId, "transport-attempt");
                if (TrySubmitNow(parts, Submit, out var retryableFailure))
                {
                    Trace(operationId, "commit");
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

                var pendingSubmit = _operationFactory.CreateRequest(
                    parts,
                    Submit,
                    completion,
                    operationId);
                if (retryableFailure is not null) pendingSubmit.RecordSubmitFailure(retryableFailure);

                ownsParts = false;
                EnqueuePending(pendingSubmit, cancellationToken);
                return await completion.Task.ConfigureAwait(false);
            }
            finally
            {
                EndInitialAttempt();
            }
        }
        catch
        {
            if (ownsParts) ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    private void OnSendReady()
    {
        lock (_gate)
        {
            if (!Volatile.Read(ref _accepting)) return;
            if (_initialAttempts == 0 && _pending.Count == 0) return;
            if (_readyCredits < long.MaxValue) _readyCredits++;
        }

        Drain();
    }

    private void BeginInitialAttempt()
    {
        lock (_gate) _initialAttempts++;
    }

    private void EndInitialAttempt()
    {
        lock (_gate)
        {
            _initialAttempts--;
            if (_initialAttempts == 0 && _pending.Count == 0) _readyCredits = 0;
        }

        Drain();
    }

    private void EnqueuePending(PendingSubmit pending, CancellationToken cancellationToken)
    {
        try
        {
            // Activate before publishing the item to the queue. A ready
            // callback must never observe an item whose cancellation and
            // deadline terminal paths are not registered yet.
            pending.Activate(cancellationToken, _stopToken);
            lock (_gate)
            {
                if (!_pending.TryEnqueue(pending))
                    throw new ZLinkPendingAdmissionFullException();
                Trace(pending.OperationId, "pending");
            }
        }
        catch
        {
            pending.Dispose();
            Trace(pending.OperationId, "cleanup");
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

                lock (_gate)
                {
                    if (_readyCredits == 0) return;
                    _readyCredits--;
                }

                Trace(item.OperationId, "send-ready");
                Trace(item.OperationId, "retry-attempt", retry: true);
                Trace(item.OperationId, "transport-attempt", retry: true);

                if (!TrySubmitNow(
                        item.Parts,
                        item.TrySubmit,
                        out var retryableFailure,
                        item,
                        item.CompleteOnAccepted
                            ? () =>
                            {
                                Trace(item.OperationId, "commit", retry: true);
                                item.TryComplete(null);
                            }
                            : () => Trace(item.OperationId, "commit", retry: true)))
                {
                    if (item.IsCompleted)
                    {
                        Dequeue(item);
                        continue;
                    }

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

                    continue;
                }

                Dequeue(item);
            }
        }
        finally
        {
            var continueDrain = false;
            lock (_gate)
            {
                _draining = false;
                if (ReferenceEquals(_activeDrainCompletion, drainCompletion))
                    _activeDrainCompletion = null;
                continueDrain = Volatile.Read(ref _accepting)
                                && _readyCredits > 0
                                && _pending.Count > 0;
            }
            drainCompletion.TrySetResult();
            if (continueDrain) Drain();
        }
    }

    private bool TrySubmitNow(
        IReadOnlyList<Message> parts,
        Func<IReadOnlyList<Message>, bool> trySubmit,
        out ZlinkException? retryableFailure,
        PendingSubmit? pending = null,
        Action? onAccepted = null)
    {
        lock (_submitGate)
        {
            ObjectDisposedException.ThrowIf(!_accepting, this);
            retryableFailure = null;
            IReadOnlyList<Message>? attempt = null;
            try
            {
                if (pending?.IsCompleted == true) return false;
                attempt = ZLinkMessageParts.CopyAll(parts);
                var accepted = trySubmit(attempt);
                if (accepted) onAccepted?.Invoke();
                return accepted;
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
        if (_pending.TryDequeue(expected, out var pending) && pending is not null)
        {
            pending.Dispose();
            Trace(pending.OperationId, "cleanup");
        }
    }

    private void OnPendingTerminal(PendingSubmit expected)
    {
        if (_pending.TryRemove(expected, out var pending) && pending is not null)
        {
            pending.Dispose();
            Trace(pending.OperationId, "cleanup");
        }
        Drain();
    }

    private void Trace(string operationId, string eventName, bool retry = false) =>
        ZLinkTelemetry.TraceSubmitAdmission(
            operationId,
            eventName,
            _pending.Count,
            retry);

    private static TimeSpan ValidateTimeout(TimeSpan? timeout)
    {
        try
        {
            return ZLinkSocketConfig.NormalizeSendTimeout(timeout)
                   ?? TimeSpan.FromSeconds(1);
        }
        catch (ZLinkConfigurationException error)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), timeout, error.Message);
        }
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
