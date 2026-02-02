using System;
using Zlink.Native;

namespace Zlink;

public sealed class Socket : IDisposable
{
    private IntPtr _handle;

    public Socket(Context context, int type)
    {
        _handle = NativeMethods.zlink_socket(context.Handle, type);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public IntPtr Handle => _handle;

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_close(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Socket()
    {
        Dispose();
    }
}
