// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink.Sockets.Internal;

internal sealed class SocketHandle : IDisposable
{
    private IntPtr _handle;
    private readonly bool _own;

    public SocketHandle(Context context, SocketType type)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));

        _handle = NativeMethods.zlink_socket(context.Handle, (int)type);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        _own = true;
    }

    public SocketHandle(IntPtr handle, bool own)
    {
        if (handle == IntPtr.Zero)
            throw new ArgumentException("Invalid socket handle.", nameof(handle));

        _handle = handle;
        _own = own;
    }

    public IntPtr DangerousGetHandle()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(SocketHandle));
        return _handle;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;

        if (_own)
            NativeMethods.zlink_close(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~SocketHandle()
    {
        try
        {
            Dispose();
        }
        catch
        {
        }
    }
}
