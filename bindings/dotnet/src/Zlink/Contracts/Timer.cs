// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Concurrent;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class Timer : IDisposable, IAsyncDisposable
{
    private static readonly ConcurrentDictionary<IntPtr, WeakReference<Timer>>
        TimersByHandle = new();
    private IntPtr _handle;
    private readonly bool _ownsHandle;
    private Action<Timer, ulong>? _handler;
    private SynchronizationContext? _handlerContext;
    private NativeMethods.ZlinkTimerHandlerDelegate? _handlerNative;

    public Timer()
    {
        _ownsHandle = true;
        _handle = NativeMethods.zlink_timer_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        RegisterHandle();
    }

    private Timer(IntPtr handle, bool ownsHandle)
    {
        _handle = handle;
        _ownsHandle = ownsHandle;
        if (ownsHandle)
            RegisterHandle();
    }

    internal IntPtr Handle
    {
        get
        {
            EnsureNotDisposed();
            return _handle;
        }
    }

    public static Timer FromSpot(Spot spot)
    {
        if (spot == null)
            throw new ArgumentNullException(nameof(spot));

        IntPtr handle = NativeMethods.zlink_spot_timer_new(spot.Handle);
        if (handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
        return new Timer(handle, ownsHandle: true);
    }

    internal static Timer? FromDispatchSubject(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return null;

        if (TimersByHandle.TryGetValue(handle, out WeakReference<Timer>? weak)
            && weak.TryGetTarget(out Timer? timer))
        {
            return timer;
        }

        return new Timer(handle, ownsHandle: false);
    }

    public void Start(TimeSpan interval, ulong repeatCount)
    {
        if (interval < TimeSpan.Zero
            || interval.Ticks > (long)(ulong.MaxValue / 100UL))
        {
            throw new ArgumentOutOfRangeException(nameof(interval));
        }

        Start((ulong)interval.Ticks * 100UL, repeatCount);
    }

    internal void Start(ulong intervalNs, ulong repeatCount)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_timer_start(_handle, intervalNs,
            repeatCount);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public void Stop()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_timer_stop(_handle);
        if (rc != 0)
            throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    public ulong? Recv(RecvFlags flags = RecvFlags.None)
    {
        EnsureNotDisposed();
        if ((flags & RecvFlags.DontWait) != 0 && !PollReadyNoWait())
            return null;

        int rc = NativeMethods.zlink_timer_recv(_handle, out ulong fireCount);
        if (rc != 0)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
        return fireCount;
    }

    public void OnFire(Action<Timer, ulong> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        _handler = handler;
        _handlerContext = SynchronizationContext.Current;
        if (_handlerNative != null)
            return;

        _handlerNative = OnNativeFire;
        int rc = NativeMethods.zlink_timer_handler(_handle, _handlerNative,
            IntPtr.Zero);
        if (rc != 0)
        {
            _handler = null;
            _handlerContext = null;
            _handlerNative = null;
            throw ZlinkException.CreateHandlerException(NativeMethods.zlink_errno());
        }
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Timer()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        if (!_ownsHandle)
        {
            _handle = IntPtr.Zero;
            _handler = null;
            _handlerContext = null;
            _handlerNative = null;
            return;
        }

        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_timer_destroy(ref handle);
        if (rc == 0)
        {
            UnregisterHandle(originalHandle);
            _handle = IntPtr.Zero;
            _handler = null;
            _handlerContext = null;
            _handlerNative = null;
            return;
        }

        _handle = originalHandle;
        if (throwOnError)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }

    private void RegisterHandle()
    {
        if (_handle != IntPtr.Zero)
            TimersByHandle[_handle] = new WeakReference<Timer>(this);
    }

    private static void UnregisterHandle(IntPtr handle)
    {
        if (handle != IntPtr.Zero)
            TimersByHandle.TryRemove(handle, out _);
    }

    private bool PollReadyNoWait()
    {
        IntPtr poller = NativeMethods.zlink_poller_new();
        if (poller == IntPtr.Zero)
            throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());

        try
        {
            int rc = NativeMethods.zlink_poller_add_timer(poller, _handle,
                IntPtr.Zero);
            if (rc != 0)
                throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());

            int ready = NativeMethods.zlink_poller_wait(poller,
                new ZlinkPollerEvent[1], 1, 0, out _);
            if (ready < 0)
                throw ZlinkException.CreateRecvException(NativeMethods.zlink_errno());
            return ready > 0;
        }
        finally
        {
            if (poller != IntPtr.Zero)
            {
                _ = NativeMethods.zlink_poller_remove_timer(poller, _handle);
                _ = NativeMethods.zlink_poller_destroy(ref poller);
            }
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Timer));
    }

    private void OnNativeFire(IntPtr timer, ulong fireCount, IntPtr userData)
    {
        _ = timer;
        _ = userData;

        Action<Timer, ulong>? handler = _handler;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(_handlerContext, () => handler(this, fireCount));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }
}
