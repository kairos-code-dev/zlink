// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading.Tasks;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class ZlinkStopwatch : IZlinkStopwatch
{
    private IntPtr _handle;

    public ZlinkStopwatch()
    {
        _handle = NativeMethods.zlink_stopwatch_start();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
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

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
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
