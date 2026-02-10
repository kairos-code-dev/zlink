// SPDX-License-Identifier: MPL-2.0

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

    public void SetOption(ContextOption option, int value)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_ctx_set(_handle, (int)option, value);
        ZlinkException.ThrowIfError(rc);
    }

    public int GetOption(ContextOption option)
    {
        EnsureNotDisposed();
        int value = NativeMethods.zlink_ctx_get(_handle, (int)option);
        if (value < 0)
            throw ZlinkException.FromLastError();
        return value;
    }

    public void Shutdown()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_ctx_shutdown(_handle);
        ZlinkException.ThrowIfError(rc);
    }

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

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Context));
    }
}
