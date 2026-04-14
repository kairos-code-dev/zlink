// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading;
using System.Threading.Tasks;

namespace Zlink;

internal sealed class RequestCallState
{
    private CancellationTokenRegistration _cancellationRegistration;
    private System.Threading.Timer? _timeoutTimer;
    private int _completed;

    internal RequestCallState(TaskCompletionSource<Received> completion)
    {
        Completion = completion;
    }

    internal TaskCompletionSource<Received> Completion { get; }

    internal bool TrySetResult(Received received)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetResult(received);
    }

    internal bool TrySetException(Exception error)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetException(error);
    }

    internal bool TrySetCanceled(CancellationToken token)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
            return false;
        DisposeRegistrations();
        return Completion.TrySetCanceled(token);
    }

    internal void SetCancellationRegistration(
        CancellationTokenRegistration cancellationRegistration)
    {
        _cancellationRegistration = cancellationRegistration;
    }

    internal void SetTimeoutTimer(System.Threading.Timer? timeoutTimer)
    {
        _timeoutTimer = timeoutTimer;
    }

    private void DisposeRegistrations()
    {
        _timeoutTimer?.Dispose();
        _cancellationRegistration.Dispose();
    }
}
