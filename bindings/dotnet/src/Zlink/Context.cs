using System;
using Zlink.Native;

namespace Zlink;

public sealed class Context : IDisposable
{
    private IntPtr _handle;

    public Context()
    {
        _handle = NativeMethods.zlink_ctx_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_ctx_term(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~Context()
    {
        Dispose();
    }
}
