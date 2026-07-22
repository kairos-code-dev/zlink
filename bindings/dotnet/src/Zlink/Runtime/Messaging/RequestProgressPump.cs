// SPDX-License-Identifier: MPL-2.0

using System.Collections.Concurrent;
using System.Diagnostics;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal static class RequestProgressPump
{
    private const short PollCompletion = 32;
    private const int PollerEventBatch = 64;
    private const int PollRecheckTimeoutMs = 10;

    private static readonly long IdleKeepaliveTicks =
        Stopwatch.Frequency;

    private static readonly ConcurrentDictionary<nint, ProgressState> States = new();
    private static readonly ConcurrentDictionary<nint, int> ExternalProgress = new();

    internal static Task<T> AttachSocket<T>(IntPtr handle, Task<T> task)
    {
        return Attach(handle, task);
    }

    internal static ProgressLease AttachSocketCallback(IntPtr handle)
    {
        return Attach(handle);
    }

    private static ProgressLease Attach(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return default;

        var key = handle;
        if (ExternalProgressCount(key) > 0)
            return default;
        var state = States.GetOrAdd(key, _ => new ProgressState());
        Interlocked.Increment(ref state.ActiveCount);
        EnsureWorker(key, state, handle);
        return new ProgressLease(key, state);
    }

    private static Task<T> Attach<T>(IntPtr handle, Task<T> task)
    {
        if (handle == IntPtr.Zero || task.IsCompleted)
            return task;

        var key = handle;
        if (ExternalProgressCount(key) > 0)
            return task;
        var state = States.GetOrAdd(key, _ => new ProgressState());
        Interlocked.Increment(ref state.ActiveCount);
        EnsureWorker(key, state, handle);

        _ = task.ContinueWith(completedTask =>
            {
                if (Interlocked.Decrement(ref state.ActiveCount) == 0
                    && Volatile.Read(ref state.WorkerRunning) == 0)
                    States.TryRemove(key, out _);
            }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);

        return task;
    }

    internal static void AcquireExternalProgress(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return;
        AddExternalProgress(handle);
    }

    internal static void ReleaseExternalProgress(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return;
        RemoveExternalProgress(handle);
    }

    private static int ExternalProgressCount(nint key)
    {
        // External poll loops already drive completion for their handles;
        // starting a private pump would split ownership of the same readiness.
        return ExternalProgress.TryGetValue(key, out var count) ? count : 0;
    }

    private static void AddExternalProgress(IntPtr handle)
    {
        var key = handle;
        ExternalProgress.AddOrUpdate(key, 1, (_, count) => count + 1);
    }

    private static void RemoveExternalProgress(IntPtr handle)
    {
        var key = handle;
        while (ExternalProgress.TryGetValue(key, out var count))
        {
            if (count <= 1)
            {
                if (ExternalProgress.TryRemove(key, out _))
                    return;
                continue;
            }

            if (ExternalProgress.TryUpdate(key, count - 1, count))
                return;
        }
    }

    private static void EnsureWorker(nint key, ProgressState state,
        IntPtr handle)
    {
        if (Interlocked.CompareExchange(ref state.WorkerRunning, 1, 0) != 0)
            return;

        Thread worker = new(() =>
        {
            var poller = IntPtr.Zero;
            try
            {
                poller = NativeMethods.zlink_poller_new();
                if (poller == IntPtr.Zero)
                    return;
                if (NativeMethods.zlink_poller_add(poller, handle,
                        IntPtr.Zero, PollCompletion) != 0)
                    return;
                var events =
                    new ZlinkPollerEvent[PollerEventBatch];
                long idleDeadlineTicks = 0;
                while (true)
                {
                    var activeCount = Volatile.Read(ref state.ActiveCount);
                    if (activeCount <= 0)
                    {
                        // Keep the worker alive briefly after the last task so
                        // bursty request batches do not churn background threads.
                        var nowTicks = Stopwatch.GetTimestamp();
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
                    // The progress worker does not own the socket handle.
                    // During teardown the owner can close that handle
                    // before this idle worker exits, especially on slower CI
                    // runners. Destroying the private poller is enough to drop
                    // its registrations; passing the possibly closed handle
                    // back into zlink_poller_remove can race native teardown.
                    try
                    {
                        _ = NativeMethods.zlink_poller_destroy(ref poller);
                    }
                    catch
                    {
                    }

                Interlocked.Exchange(ref state.WorkerRunning, 0);
                if (Volatile.Read(ref state.ActiveCount) == 0)
                    States.TryRemove(key, out _);
                else
                    EnsureWorker(key, state, handle);
            }
        })
        {
            IsBackground = true,
            Name = "Zlink request progress"
        };
        worker.Start();
    }

    internal sealed class ProgressState
    {
        internal int ActiveCount;
        internal int WorkerRunning;
    }

    internal readonly struct ProgressLease
    {
        private readonly nint _key;
        private readonly ProgressState _state;
        private readonly bool _active;

        internal ProgressLease(nint key, ProgressState state)
        {
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
                States.TryRemove(_key, out _);
        }
    }
}
