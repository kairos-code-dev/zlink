// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink.Service;

public sealed class ServiceMonitor : IDisposable
{
    private IntPtr _handle;
    private NativeMethods.ZlinkServiceMonitorHandlerDelegate? _handlerDelegate;
    private Action<ServiceMonitorEvent>? _handler;

    internal ServiceMonitor(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid monitor handle.", nameof(handle));
        _handle = handle;
    }

    public void AttachHandler(Action<ServiceMonitorEvent> handler)
    {
        if (handler == null)
            throw new ArgumentNullException(nameof(handler));
        EnsureNotDisposed();

        _handler = handler;
        _handlerDelegate = OnNativeEvent;
        int rc = NativeMethods.zlink_service_monitor_handler(_handle,
            _handlerDelegate, IntPtr.Zero);
        ZlinkException.ThrowIfError(rc);
    }

    public ServiceMonitorEvent Receive()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_service_monitor_recv(_handle, out var native);
        ZlinkException.ThrowIfError(rc);
        return ServiceMonitorEvent.FromNative(ref native);
    }

    public ServiceMonitorEvent? TryReceive()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_try_service_monitor_recv(_handle,
            out var native);
        if (rc == 0)
            return ServiceMonitorEvent.FromNative(ref native);
        if (ZlinkException.MapErrorCode(NativeMethods.zlink_errno())
            == ErrorCode.EAgain)
        {
            return null;
        }
        throw ZlinkException.FromLastError();
    }

    public void Close()
    {
        if (_handle == IntPtr.Zero)
            return;
        int rc = NativeMethods.zlink_monitor_close(ref _handle);
        ZlinkException.ThrowIfError(rc);
        _handler = null;
        _handlerDelegate = null;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        try
        {
            Close();
        }
        finally
        {
            GC.SuppressFinalize(this);
        }
    }

    ~ServiceMonitor()
    {
        try
        {
            Close();
        }
        catch
        {
        }
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(ServiceMonitor));
    }

    private void OnNativeEvent(ref ZlinkServiceEvent native, IntPtr userData)
    {
        Action<ServiceMonitorEvent>? handler = _handler;
        if (handler == null)
            return;

        try
        {
            handler(ServiceMonitorEvent.FromNative(ref native));
        }
        catch (Exception ex)
        {
            Runtime.ReportUnhandledCallbackException(ex);
        }
    }
}
