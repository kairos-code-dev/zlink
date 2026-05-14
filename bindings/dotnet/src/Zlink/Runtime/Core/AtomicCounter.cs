// SPDX-License-Identifier: MPL-2.0

using System;
using System.Threading.Tasks;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class AtomicCounter : IAtomicCounter
{
    private IntPtr _handle;

    public AtomicCounter()
    {
        _handle = NativeMethods.zlink_atomic_counter_new();
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    public int Value
    {
        get
        {
            EnsureNotDisposed();
            return NativeMethods.zlink_atomic_counter_value(_handle);
        }
    }

    public void Set(int value)
    {
        EnsureNotDisposed();
        NativeMethods.zlink_atomic_counter_set(_handle, value);
    }

    public int Increment()
    {
        EnsureNotDisposed();
        return NativeMethods.zlink_atomic_counter_inc(_handle);
    }

    public int Decrement()
    {
        EnsureNotDisposed();
        return NativeMethods.zlink_atomic_counter_dec(_handle);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_atomic_counter_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~AtomicCounter()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(AtomicCounter));
    }
}
