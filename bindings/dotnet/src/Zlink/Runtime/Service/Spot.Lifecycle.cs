// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed partial class Spot : ISpot
{
    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Spot()
    {
        Destroy(false);
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;

        var originalHandle = Handle;
        var handle = Handle;
        var rc = _ownsHandle
            ? NativeMethods.zlink_spot_destroy(ref handle)
            : 0;
        if (rc != 0)
        {
            Handle = originalHandle;
            if (throwOnError)
                throw ZlinkException.CreateCloseException(
                    NativeMethods.zlink_errno());
            return;
        }

        Handle = IntPtr.Zero;
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
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Spot));
    }

    private void OnNativeSendReady(IntPtr subject, IntPtr userData)
    {
        var handler = _sendReadyHandler;
        var context = _sendReadyHandlerContext;
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
                                            ex.NativeErrno) is ErrorCode.EAgain or ErrorCode.EBusy)
        {
            return null;
        }
    }
}