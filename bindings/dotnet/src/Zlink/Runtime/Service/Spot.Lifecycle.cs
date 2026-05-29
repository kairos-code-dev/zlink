// SPDX-License-Identifier: MPL-2.0

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Systems.Zlink.Native;
using Systems.Zlink.Sockets.Internal;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
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

    ~Spot()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = _ownsHandle
            ? NativeMethods.zlink_spot_destroy(ref handle)
            : 0;
        if (rc != 0)
        {
            _handle = originalHandle;
            if (throwOnError)
            {
                throw ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
            }
            return;
        }

        _handle = IntPtr.Zero;
        _sendReadyHandler = null;
        _dispatchEventHandler = null;
        _sendReadyHandlerContext = null;
        _sendReadyHandlerNative = null;
        _dispatchEventHandlerNative = null;
        _dispatchEventHandlerInstalled = false;
        _node.UnregisterSpot(this);
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        Action? handler = _sendReadyHandler;
        SynchronizationContext? context = _sendReadyHandlerContext;
        if (handler == null)
            return;

        try
        {
            CallbackDelivery.Post(context, handler);
        }
        catch (Exception ex)
        {
            CallbackExceptionHub.Report(ex);
        }
    }

    private static T? TryReceiveCore<T>(Func<T> operation) where T : class
    {
        try
        {
            return operation();
        }
        catch (ZlinkException ex) when (ZlinkException.MapErrorCode(
            ex.InternalErrno) is ErrorCode.EAgain or ErrorCode.EBusy)
        {
            return null;
        }
    }
}
