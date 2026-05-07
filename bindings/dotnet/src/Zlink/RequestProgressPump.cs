// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

internal static class RequestProgressPump
{
    private sealed class ProgressState
    {
        internal int ActiveCount;
        internal int WorkerRunning;
    }

    private static readonly ConcurrentDictionary<nint, ProgressState> SocketStates = new();
    private static readonly ConcurrentDictionary<nint, ProgressState> SpotStates = new();

    internal static Task<T> AttachSocket<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SocketStates, handle, task,
            NativeMethods.zlink_socket_request_progress_internal);
    }

    internal static Task<T> AttachSpot<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SpotStates, handle, task,
            NativeMethods.zlink_spot_request_progress_internal);
    }

    private static Task<T> Attach<T>(ConcurrentDictionary<nint, ProgressState> states,
        IntPtr handle, Task<T> task, Func<IntPtr, int> progress)
    {
        if (handle == IntPtr.Zero || task.IsCompleted)
            return task;

        nint key = handle;
        ProgressState state = states.GetOrAdd(key, _ => new ProgressState());
        Interlocked.Increment(ref state.ActiveCount);
        EnsureWorker(states, key, state, handle, progress);

        _ = task.ContinueWith(completedTask =>
        {
            if (Interlocked.Decrement(ref state.ActiveCount) == 0
                && Volatile.Read(ref state.WorkerRunning) == 0)
            {
                states.TryRemove(key, out _);
            }
        }, TaskScheduler.Default);

        return task;
    }

    private static void EnsureWorker(
        ConcurrentDictionary<nint, ProgressState> states,
        nint key,
        ProgressState state,
        IntPtr handle,
        Func<IntPtr, int> progress)
    {
        if (Interlocked.CompareExchange(ref state.WorkerRunning, 1, 0) != 0)
            return;

        _ = Task.Run(async () =>
        {
            try
            {
                while (Volatile.Read(ref state.ActiveCount) > 0)
                {
                    try
                    {
                        _ = progress(handle);
                    }
                    catch
                    {
                    }

                    await Task.Delay(1).ConfigureAwait(false);
                }

                try
                {
                    _ = progress(handle);
                }
                catch
                {
                }
            }
            finally
            {
                Interlocked.Exchange(ref state.WorkerRunning, 0);
                if (Volatile.Read(ref state.ActiveCount) == 0)
                    states.TryRemove(key, out _);
                else
                    EnsureWorker(states, key, state, handle, progress);
            }
        });
    }
}
