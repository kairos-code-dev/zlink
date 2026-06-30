// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink;

internal sealed class RequestCallState
{
    private CancellationTokenRegistration _cancellationRegistration;

    // Native completion, user cancellation, and timeout all share this gate;
    // the first terminal edge owns cleanup for the managed registrations.
    private int _completed;
    private System.Threading.Timer? _timeoutTimer;

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

    internal void Dispose()
    {
        DisposeRegistrations();
    }

    internal static RequestCallState? FromUserData(object? userdata)
    {
        if (userdata is not GCHandle handle || !handle.IsAllocated)
            return null;

        try
        {
            return handle.Target as RequestCallState;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    internal static void CancelFromUserData(object? userdata)
    {
        FromUserData(userdata)?.TrySetCanceled(CancellationToken.None);
    }

    internal static void TimeoutFromUserData(object? userdata)
    {
        FromUserData(userdata)?.TrySetException(
            new ZlinkRequestException(RequestResult.TimedOut));
    }

    internal static void RequestTimeoutFromUserData(object? userdata)
    {
        FromUserData(userdata)?.TrySetException(new ZlinkRequestException(
            RequestResult.TimedOut, (int)ErrorCode.ETimedOut));
    }

    private void DisposeRegistrations()
    {
        _timeoutTimer?.Dispose();
        _cancellationRegistration.Dispose();
    }
}