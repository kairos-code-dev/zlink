// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class RequestProgressPump
{
    private const short PollCompletion = 32;

    private sealed class ProgressState
    {
        internal int ActiveCount;
        internal int WorkerRunning;
    }

    private static readonly ConcurrentDictionary<nint, ProgressState> SocketStates = new();
    private static readonly ConcurrentDictionary<nint, ProgressState> SpotStates = new();

    internal static Task<T> AttachSocket<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SocketStates, handle, task);
    }

    internal static Task<T> AttachSpot<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SpotStates, handle, task);
    }

    private static Task<T> Attach<T>(ConcurrentDictionary<nint, ProgressState> states,
        IntPtr handle, Task<T> task)
    {
        if (handle == IntPtr.Zero || task.IsCompleted)
            return task;

        nint key = handle;
        ProgressState state = states.GetOrAdd(key, _ => new ProgressState());
        Interlocked.Increment(ref state.ActiveCount);
        EnsureWorker(states, key, state, handle);

        _ = task.ContinueWith(completedTask =>
        {
            if (Interlocked.Decrement(ref state.ActiveCount) == 0
                && Volatile.Read(ref state.WorkerRunning) == 0)
            {
                states.TryRemove(key, out _);
            }
        }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);

        return task;
    }

    private static void EnsureWorker(
        ConcurrentDictionary<nint, ProgressState> states,
        nint key,
        ProgressState state,
        IntPtr handle)
    {
        if (Interlocked.CompareExchange(ref state.WorkerRunning, 1, 0) != 0)
            return;

        _ = Task.Run(async () =>
        {
            IntPtr poller = IntPtr.Zero;
            try
            {
                poller = NativeMethods.zlink_poller_new();
                if (poller == IntPtr.Zero)
                    return;
                if (NativeMethods.zlink_poller_add(poller, handle,
                    IntPtr.Zero, PollCompletion) != 0)
                    return;
                ZlinkPollerEvent[] events = new ZlinkPollerEvent[1];
                while (Volatile.Read(ref state.ActiveCount) > 0)
                {
                    try
                    {
                        _ = NativeMethods.zlink_poller_wait(poller, events, 1,
                            -1, out _);
                    }
                    catch
                    {
                        break;
                    }
                }
                await Task.CompletedTask.ConfigureAwait(false);
            }
            finally
            {
                if (poller != IntPtr.Zero)
                {
                    try { _ = NativeMethods.zlink_poller_remove(poller, handle); }
                    catch { }
                    try { _ = NativeMethods.zlink_poller_destroy(ref poller); }
                    catch { }
                }
                Interlocked.Exchange(ref state.WorkerRunning, 0);
                if (Volatile.Read(ref state.ActiveCount) == 0)
                    states.TryRemove(key, out _);
                else
                    EnsureWorker(states, key, state, handle);
            }
        });
    }
}
