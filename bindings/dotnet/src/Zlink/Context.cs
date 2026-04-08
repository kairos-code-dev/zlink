// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class Context : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public Context()
    {
        Options = new ContextOptions(this);
        _handle = NativeMethods.zlink_ctx_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public ContextOptions Options { get; }

    internal void SetOption(ContextOption option, int value)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
        int rc = NativeMethods.zlink_ctx_set(_handle, (int)option, value);
        ZlinkException.ThrowIfError(rc);
    }

    internal int GetOption(ContextOption option)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureContextOption(option, nameof(option));
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

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
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
