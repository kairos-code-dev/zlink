namespace Zlink.Framework.Runtime.Dispatch;

internal sealed class ZLinkInboundDispatchBudget
{
    private readonly object _gate = new();
    private readonly SemaphoreSlim _receiveCapacity = new(1, 1);
    private readonly ulong _applicationHwmBytes;
    private ulong _pendingPayloadBytes;
    private ulong _activePayloadBytes;
    private bool _receivePaused;
    private List<Action>? _capacityAvailableHandlers;

    internal ZLinkInboundDispatchBudget(ulong applicationHwmBytes)
    {
        _applicationHwmBytes = applicationHwmBytes;
    }

    internal bool CanStartApplicationReceive
        => CanStartApplicationReceiveNow();

    internal ulong PendingPayloadBytes
        => Volatile.Read(ref _pendingPayloadBytes);

    internal async ValueTask<bool> WaitForReceiveCapacityAsync(
        CancellationToken cancellationToken)
    {
        while (true)
        {
            if (CanStartApplicationReceiveNow()
                && !Volatile.Read(ref _receivePaused))
                return false;

            await _receiveCapacity.WaitAsync(cancellationToken)
                .ConfigureAwait(false);
            // The permit represents one receive attempt released by a
            // completed dispatch. A concurrent receive may cross the HWM
            // again before this waiter resumes; the attempt still owns the
            // permit and CompleteReceiveAttempt reconciles the next state.
            return true;
        }
    }

    internal void CompleteReceiveAttempt(bool ownsResumePermit)
    {
        if (!ownsResumePermit)
            return;
        lock (_gate)
        {
            if (CanStartApplicationReceiveUnderLock())
            {
                _receivePaused = false;
                ReleaseReceiveCapacityUnderLock();
            }
            else
            {
                _receivePaused = true;
                _receiveCapacity.Wait(0);
            }
        }
    }

    internal void Received(ulong payloadBytes)
    {
        var pending = AddPendingPayload(payloadBytes);
        MarkReceivePausedIfNeeded(pending);
    }

    internal bool TryTrack(
        ulong payloadBytes,
        out ZLinkInboundDispatchLease? lease)
    {
        while (true)
        {
            var current = Volatile.Read(ref _pendingPayloadBytes);
            // A complete message that began below HWM may finish above it;
            // the contract stops the next application receive, so admission
            // checks the immutable pending total before this message.
            if (_applicationHwmBytes != 0
                && current >= _applicationHwmBytes)
            {
                lease = null;
                return false;
            }

            var next = checked(current + payloadBytes);
            if (Interlocked.CompareExchange(
                    ref _pendingPayloadBytes,
                    next,
                    current) != current)
                continue;
            MarkReceivePausedIfNeeded(next);
            lease = new ZLinkInboundDispatchLease(this, payloadBytes);
            return true;
        }
    }

    internal void HandlerStarted(ulong payloadBytes)
    {
        AddActivePayload(payloadBytes);
    }

    internal void Completed(ulong payloadBytes, bool handlerStarted)
    {
        Action[]? handlers = null;
        if (handlerStarted)
        {
            SubtractActivePayload(payloadBytes);
        }
        var pending = SubtractPendingPayload(payloadBytes);
        if (_applicationHwmBytes != 0
            && pending < _applicationHwmBytes)
        {
            lock (_gate)
            {
                if (_receivePaused
                    && CanStartApplicationReceiveNow())
                {
                    _receivePaused = false;
                    ReleaseReceiveCapacityUnderLock();
                    handlers = _capacityAvailableHandlers?.ToArray();
                }
            }
        }

        if (handlers is null) return;
        foreach (var handler in handlers)
        {
            try
            {
                handler();
            }
            catch
            {
                // Capacity notification must not change payload accounting or
                // turn a completed application dispatch into a runtime failure.
            }
        }
    }

    internal ZLinkInboundDispatchLease Track(ulong payloadBytes)
    {
        Received(payloadBytes);
        return new ZLinkInboundDispatchLease(this, payloadBytes);
    }

    internal ZLinkInboundDispatchLease Track(IReadOnlyList<Message> parts)
    {
        ulong payloadBytes = 0;
        foreach (var part in parts)
            payloadBytes = checked(payloadBytes + (ulong)part.Size);
        return Track(payloadBytes);
    }

    internal IDisposable RegisterCapacityAvailable(Action handler)
    {
        ArgumentNullException.ThrowIfNull(handler);
        lock (_gate)
        {
            (_capacityAvailableHandlers ??= []).Add(handler);
        }
        return new CapacityRegistration(this, handler);
    }

    private void UnregisterCapacityAvailable(Action handler)
    {
        lock (_gate)
            _capacityAvailableHandlers?.Remove(handler);
    }

    private sealed class CapacityRegistration(
        ZLinkInboundDispatchBudget owner,
        Action handler) : IDisposable
    {
        private int _disposed;

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
                owner.UnregisterCapacityAvailable(handler);
        }
    }

    internal ZLinkInboundDispatchBudgetSnapshot Snapshot()
    {
        var pending = Volatile.Read(ref _pendingPayloadBytes);
        var active = Math.Min(Volatile.Read(ref _activePayloadBytes), pending);
        return new ZLinkInboundDispatchBudgetSnapshot(
            _applicationHwmBytes,
            pending,
            pending - active,
            active,
            Volatile.Read(ref _receivePaused));
    }

    private bool CanStartApplicationReceiveNow() =>
        _applicationHwmBytes == 0
        || Volatile.Read(ref _pendingPayloadBytes) < _applicationHwmBytes;

    private bool CanStartApplicationReceiveUnderLock() =>
        _applicationHwmBytes == 0
        || Volatile.Read(ref _pendingPayloadBytes) < _applicationHwmBytes;

    private ulong AddPendingPayload(ulong payloadBytes) =>
        AddPayload(ref _pendingPayloadBytes, payloadBytes);

    private ulong SubtractPendingPayload(ulong payloadBytes) =>
        SubtractPayload(ref _pendingPayloadBytes, payloadBytes);

    private void AddActivePayload(ulong payloadBytes) =>
        _ = AddPayload(ref _activePayloadBytes, payloadBytes);

    private void SubtractActivePayload(ulong payloadBytes) =>
        _ = SubtractPayload(ref _activePayloadBytes, payloadBytes);

    private void MarkReceivePausedIfNeeded(ulong pendingPayloadBytes)
    {
        if (_applicationHwmBytes == 0
            || pendingPayloadBytes < _applicationHwmBytes)
            return;

        lock (_gate)
        {
            if (Volatile.Read(ref _pendingPayloadBytes) >= _applicationHwmBytes
                && !_receivePaused)
            {
                _receivePaused = true;
                _receiveCapacity.Wait(0);
            }
        }
    }

    private static ulong AddPayload(ref ulong target, ulong payloadBytes)
    {
        while (true)
        {
            var current = Volatile.Read(ref target);
            var next = unchecked(current + payloadBytes);
            if (Interlocked.CompareExchange(ref target, next, current)
                == current)
                return next;
        }
    }

    private static ulong SubtractPayload(ref ulong target, ulong payloadBytes)
    {
        while (true)
        {
            var current = Volatile.Read(ref target);
            var next = unchecked(current - payloadBytes);
            if (Interlocked.CompareExchange(ref target, next, current)
                == current)
                return next;
        }
    }

    private void ReleaseReceiveCapacityUnderLock()
    {
        if (_receiveCapacity.CurrentCount == 0)
            _receiveCapacity.Release();
    }

}

internal readonly record struct ZLinkInboundDispatchBudgetSnapshot(
    ulong ApplicationHwmBytes,
    ulong PendingPayloadBytes,
    ulong QueuedPayloadBytes,
    ulong ActivePayloadBytes,
    bool ApplicationReceivePaused);
