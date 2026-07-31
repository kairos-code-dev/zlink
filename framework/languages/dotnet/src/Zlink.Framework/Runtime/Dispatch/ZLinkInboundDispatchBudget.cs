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
    }

    internal void HandlerStarted(ulong payloadBytes)
    {
        lock (_gate)
            _activePayloadBytes = unchecked(_activePayloadBytes + payloadBytes);
    }

    internal void Completed(ulong payloadBytes, bool handlerStarted)
    {
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
            }
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
