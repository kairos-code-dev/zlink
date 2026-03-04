// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink;

public delegate void TimerHandler(int timerId);

public sealed class Timers : IDisposable
{
    private static readonly string[] RequiredExports = new[]
    {
        "zlink_timers_new",
        "zlink_timers_destroy",
        "zlink_timers_add",
        "zlink_timers_cancel",
        "zlink_timers_set_interval",
        "zlink_timers_reset",
        "zlink_timers_timeout",
        "zlink_timers_execute"
    };

    private sealed class TimerRegistration
    {
        public TimerRegistration(TimerHandler handler)
        {
            Handler = handler;
        }

        public TimerHandler Handler { get; }
    }

    private static readonly NativeMethods.ZlinkTimerDelegate s_timerThunk =
        OnNativeTimer;

    private readonly object _sync = new();
    private readonly Dictionary<int, GCHandle> _timerHandles = new();
    private IntPtr _handle;

    public Timers()
    {
        List<string> missing = GetMissingExports();
        if (missing.Count > 0)
        {
            throw new NotSupportedException(
                "Timers API is not available in the loaded native zlink library. "
                + "Missing exports: " + string.Join(", ", missing));
        }
        _handle = NativeMethods.zlink_timers_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public int Add(int intervalMs, TimerHandler handler)
    {
        EnsureNotDisposed();
        if (intervalMs <= 0)
            throw new ArgumentOutOfRangeException(nameof(intervalMs));
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));

        TimerRegistration registration = new(handler);
        GCHandle gcHandle = GCHandle.Alloc(registration);
        int timerId = NativeMethods.zlink_timers_add(_handle, (nuint)intervalMs,
            s_timerThunk, GCHandle.ToIntPtr(gcHandle));
        if (timerId < 0)
        {
            gcHandle.Free();
            throw ZlinkException.FromLastError();
        }

        lock (_sync)
        {
            _timerHandles[timerId] = gcHandle;
        }
        return timerId;
    }

    public int Add(TimeSpan interval, TimerHandler handler)
    {
        long totalMs = checked((long)interval.TotalMilliseconds);
        if (totalMs <= 0 || totalMs > int.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(interval));
        return Add((int)totalMs, handler);
    }

    public void Cancel(int timerId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_timers_cancel(_handle, timerId);
        ZlinkException.ThrowIfError(rc);

        lock (_sync)
        {
            if (_timerHandles.Remove(timerId, out var gcHandle)
                && gcHandle.IsAllocated)
            {
                gcHandle.Free();
            }
        }
    }

    public void SetInterval(int timerId, int intervalMs)
    {
        EnsureNotDisposed();
        if (intervalMs <= 0)
            throw new ArgumentOutOfRangeException(nameof(intervalMs));
        int rc = NativeMethods.zlink_timers_set_interval(_handle, timerId,
            (nuint)intervalMs);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetInterval(int timerId, TimeSpan interval)
    {
        long totalMs = checked((long)interval.TotalMilliseconds);
        if (totalMs <= 0 || totalMs > int.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(interval));
        SetInterval(timerId, (int)totalMs);
    }

    public void Reset(int timerId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_timers_reset(_handle, timerId);
        ZlinkException.ThrowIfError(rc);
    }

    public long Timeout()
    {
        EnsureNotDisposed();
        return NativeMethods.zlink_timers_timeout(_handle);
    }

    public int Execute()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_timers_execute(_handle);
        ZlinkException.ThrowIfError(rc);
        return rc;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;

        try
        {
            NativeMethods.zlink_timers_destroy(ref _handle);
        }
        finally
        {
            _handle = IntPtr.Zero;
            lock (_sync)
            {
                foreach (GCHandle handle in _timerHandles.Values)
                {
                    if (handle.IsAllocated)
                        handle.Free();
                }
                _timerHandles.Clear();
            }
        }
        GC.SuppressFinalize(this);
    }

    ~Timers()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Timers));
    }

    private static void OnNativeTimer(int timerId, IntPtr arg)
    {
        if (arg == IntPtr.Zero)
            return;

        try
        {
            GCHandle gcHandle = GCHandle.FromIntPtr(arg);
            if (gcHandle.Target is TimerRegistration registration)
                registration.Handler(timerId);
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }

    private static List<string> GetMissingExports()
    {
        var missing = new List<string>();
        foreach (string symbol in RequiredExports)
        {
            if (!NativeLibraryLoader.HasExport(symbol))
                missing.Add(symbol);
        }
        return missing;
    }
}
