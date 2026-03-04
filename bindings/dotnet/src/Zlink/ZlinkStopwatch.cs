// SPDX-License-Identifier: MPL-2.0

using System;
using Zlink.Native;

namespace Zlink;

public sealed class ZlinkStopwatch : IDisposable
{
    private IntPtr _handle;

    public ZlinkStopwatch()
    {
        _handle = NativeMethods.zlink_stopwatch_start();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public ulong Intermediate()
    {
        EnsureNotDisposed();
        return NativeMethods.zlink_stopwatch_intermediate(_handle);
    }

    public ulong Stop()
    {
        EnsureNotDisposed();
        ulong elapsed = NativeMethods.zlink_stopwatch_stop(_handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
        return elapsed;
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        try
        {
            NativeMethods.zlink_stopwatch_stop(_handle);
        }
        catch
        {
        }
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~ZlinkStopwatch()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(ZlinkStopwatch));
    }
}
