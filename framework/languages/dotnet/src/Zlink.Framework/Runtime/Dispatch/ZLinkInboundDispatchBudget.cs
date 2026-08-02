namespace Zlink.Framework.Runtime.Dispatch;

internal sealed class ZLinkInboundDispatchBudget
{
    private readonly object _gate = new();
    private readonly SemaphoreSlim _receiveCapacity = new(1, 1);
    private readonly ulong _applicationHwmBytes;
    private ulong _cumulativeReceivedPayloadBytes;
    private ulong _cumulativeCompletedPayloadBytes;
    private ulong _activePayloadBytes;
    private bool _receivePaused;
    private List<Action>? _capacityAvailableHandlers;

    internal ZLinkInboundDispatchBudget(ulong applicationHwmBytes)
    {
        _applicationHwmBytes = applicationHwmBytes;
    }

    internal bool CanStartApplicationReceive
    {
        get
        {
            lock (_gate) return CanStartApplicationReceiveUnderLock();
        }
    }

    internal ulong PendingPayloadBytes
    {
        get
        {
            lock (_gate) return PendingPayloadBytesUnderLock();
        }
    }

    internal async ValueTask<bool> WaitForReceiveCapacityAsync(
        CancellationToken cancellationToken)
    {
        while (true)
        {
            lock (_gate)
                if (CanStartApplicationReceiveUnderLock() && !_receivePaused)
                    return false;

            await _receiveCapacity.WaitAsync(cancellationToken)
                .ConfigureAwait(false);
            lock (_gate)
            {
                if (!CanStartApplicationReceiveUnderLock())
                    continue;
                return true;
            }
        }
    }

    internal void CompleteReceiveAttempt(bool ownsResumePermit)
    {
        if (!ownsResumePermit)
            return;
        lock (_gate)
            if (CanStartApplicationReceiveUnderLock())
                ReleaseReceiveCapacityUnderLock();
    }

    internal void Received(ulong payloadBytes)
    {
        lock (_gate)
        {
            RecordAcceptedPayloadUnderLock(payloadBytes);
        }
    }

    internal bool TryTrack(
        ulong payloadBytes,
        out ZLinkInboundDispatchLease? lease)
    {
        lock (_gate)
        {
            // A complete message that began below HWM may finish above it;
            // the contract stops the next application receive, so admission
            // checks the immutable pending total before this message.
            if (!CanStartApplicationReceiveUnderLock())
            {
                lease = null;
                return false;
            }

            RecordAcceptedPayloadUnderLock(payloadBytes);
            lease = new ZLinkInboundDispatchLease(this, payloadBytes);
            return true;
        }
    }

    internal void HandlerStarted(ulong payloadBytes)
    {
        lock (_gate)
            _activePayloadBytes = unchecked(_activePayloadBytes + payloadBytes);
    }

    internal void Completed(ulong payloadBytes, bool handlerStarted)
    {
        Action[]? handlers = null;
        lock (_gate)
        {
            if (handlerStarted)
            {
                if (payloadBytes > _activePayloadBytes)
                    throw new InvalidOperationException();
                _activePayloadBytes -= payloadBytes;
            }
            _cumulativeCompletedPayloadBytes = unchecked(
                _cumulativeCompletedPayloadBytes + payloadBytes);

            if (_applicationHwmBytes != 0
                && _receivePaused
                && PendingPayloadBytesUnderLock() < _applicationHwmBytes)
            {
                _receivePaused = false;
                ReleaseReceiveCapacityUnderLock();
                handlers = _capacityAvailableHandlers?.ToArray();
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
        lock (_gate)
        {
            var pending = PendingPayloadBytesUnderLock();
            var active = Math.Min(_activePayloadBytes, pending);
            return new ZLinkInboundDispatchBudgetSnapshot(
                _applicationHwmBytes,
                pending,
                pending - active,
                active,
                _receivePaused);
        }
    }

    private bool CanStartApplicationReceiveUnderLock() =>
        _applicationHwmBytes == 0
        || PendingPayloadBytesUnderLock() < _applicationHwmBytes;

    private void RecordAcceptedPayloadUnderLock(ulong payloadBytes)
    {
        _cumulativeReceivedPayloadBytes = unchecked(
            _cumulativeReceivedPayloadBytes + payloadBytes);
        if (_applicationHwmBytes != 0
            && PendingPayloadBytesUnderLock() >= _applicationHwmBytes
            && !_receivePaused)
        {
            _receivePaused = true;
            _receiveCapacity.Wait(0);
        }
    }

    private ulong PendingPayloadBytesUnderLock() => Difference(
        _cumulativeReceivedPayloadBytes,
        _cumulativeCompletedPayloadBytes);

    private void ReleaseReceiveCapacityUnderLock()
    {
        if (_receiveCapacity.CurrentCount == 0)
            _receiveCapacity.Release();
    }

    private static ulong Difference(ulong received, ulong completed) =>
        unchecked((ulong)(received - completed));
}

internal readonly record struct ZLinkInboundDispatchBudgetSnapshot(
    ulong ApplicationHwmBytes,
    ulong PendingPayloadBytes,
    ulong QueuedPayloadBytes,
    ulong ActivePayloadBytes,
    bool ApplicationReceivePaused);
