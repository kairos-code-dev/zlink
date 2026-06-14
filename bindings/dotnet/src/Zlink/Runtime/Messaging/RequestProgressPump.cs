// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RequestProgressPump
{
    private const short PollCompletion = 32;
    private const int PollerEventBatch = 64;
    private const int PollRecheckTimeoutMs = 10;
    private static readonly long IdleKeepaliveTicks =
        Stopwatch.Frequency;

    internal sealed class ProgressState
    {
        internal int ActiveCount;
        internal int WorkerRunning;
    }

    private static readonly ConcurrentDictionary<nint, ProgressState> SocketStates = new();
    private static readonly ConcurrentDictionary<nint, ProgressState> SpotStates = new();
    private static readonly ConcurrentDictionary<nint, int> ExternalSocketProgress = new();
    private static readonly ConcurrentDictionary<nint, int> ExternalSpotProgress = new();

    internal static Task<T> AttachSocket<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SocketStates, handle, task);
    }

    internal static Task<T> AttachSpot<T>(IntPtr handle, Task<T> task)
    {
        return Attach(SpotStates, handle, task);
    }

    internal static ProgressLease AttachSpotCallback(IntPtr handle)
    {
        return Attach(SpotStates, handle);
    }

    private static ProgressLease Attach(
        ConcurrentDictionary<nint, ProgressState> states, IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return default;

        nint key = handle;
        if (ExternalProgressCount(states, key) > 0)
            return default;
        ProgressState state = states.GetOrAdd(key, _ => new ProgressState());
        Interlocked.Increment(ref state.ActiveCount);
        EnsureWorker(states, key, state, handle);
        return new ProgressLease(states, key, state);
    }

    private static Task<T> Attach<T>(ConcurrentDictionary<nint, ProgressState> states,
        IntPtr handle, Task<T> task)
    {
        if (handle == IntPtr.Zero || task.IsCompleted)
            return task;

        nint key = handle;
        if (ExternalProgressCount(states, key) > 0)
            return task;
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

    internal static void AcquireExternalProgress(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return;
        AddExternalProgress(ExternalSocketProgress, handle);
        AddExternalProgress(ExternalSpotProgress, handle);
    }

    internal static void ReleaseExternalProgress(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return;
        RemoveExternalProgress(ExternalSocketProgress, handle);
        RemoveExternalProgress(ExternalSpotProgress, handle);
    }

    private static int ExternalProgressCount(
        ConcurrentDictionary<nint, ProgressState> states, nint key)
    {
        // External poll loops already drive completion for their handles;
        // starting a private pump would split ownership of the same readiness.
        ConcurrentDictionary<nint, int> external =
            ReferenceEquals(states, SpotStates)
                ? ExternalSpotProgress
                : ExternalSocketProgress;
        return external.TryGetValue(key, out int count) ? count : 0;
    }

    private static void AddExternalProgress(
        ConcurrentDictionary<nint, int> external, IntPtr handle)
    {
        nint key = handle;
        external.AddOrUpdate(key, 1, (_, count) => count + 1);
    }

    private static void RemoveExternalProgress(
        ConcurrentDictionary<nint, int> external, IntPtr handle)
    {
        nint key = handle;
        while (external.TryGetValue(key, out int count))
        {
            if (count <= 1)
            {
                if (external.TryRemove(key, out _))
                    return;
                continue;
            }
            if (external.TryUpdate(key, count - 1, count))
                return;
        }
    }

    internal readonly struct ProgressLease
    {
        private readonly ConcurrentDictionary<nint, ProgressState> _states;
        private readonly nint _key;
        private readonly ProgressState _state;
        private readonly bool _active;

        internal ProgressLease(
            ConcurrentDictionary<nint, ProgressState> states, nint key,
            ProgressState state)
        {
            _states = states;
            _key = key;
            _state = state;
            _active = true;
        }

        public void Dispose()
        {
            if (!_active)
                return;
            if (Interlocked.Decrement(ref _state.ActiveCount) == 0
                && Volatile.Read(ref _state.WorkerRunning) == 0)
            {
                _states.TryRemove(_key, out _);
            }
        }
    }

    private static void EnsureWorker(
        ConcurrentDictionary<nint, ProgressState> states,
        nint key,
        ProgressState state,
        IntPtr handle)
    {
        if (Interlocked.CompareExchange(ref state.WorkerRunning, 1, 0) != 0)
            return;

        Thread worker = new(() =>
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
                ZlinkPollerEvent[] events =
                    new ZlinkPollerEvent[PollerEventBatch];
                long idleDeadlineTicks = 0;
                while (true)
                {
                    int activeCount = Volatile.Read(ref state.ActiveCount);
                    if (activeCount <= 0)
                    {
                        // Keep the worker alive briefly after the last task so
                        // bursty request batches do not churn background threads.
                        long nowTicks = Stopwatch.GetTimestamp();
                        if (idleDeadlineTicks == 0)
                            idleDeadlineTicks = nowTicks + IdleKeepaliveTicks;
                        else if (nowTicks >= idleDeadlineTicks)
                            break;
                    }
                    else
                    {
                        idleDeadlineTicks = 0;
                    }

                    try
                    {
                        _ = NativeMethods.zlink_poller_wait(poller, events,
                            events.Length,
                            activeCount > 0 ? -1 : PollRecheckTimeoutMs,
                            out _);
                    }
                    catch
                    {
                        break;
                    }
                }
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
        })
        {
            IsBackground = true,
            Name = "Zlink request progress"
        };
        worker.Start();
    }
}
