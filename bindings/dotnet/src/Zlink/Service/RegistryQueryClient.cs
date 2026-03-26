// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink.Native;

namespace Zlink.Service;

public sealed class RegistryQueryClient : IDisposable
{
    private IntPtr _handle;

    public RegistryQueryClient(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_registry_query_client_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    public void Connect(string endpoint)
    {
        if (string.IsNullOrEmpty(endpoint))
            throw new ArgumentException("Value must not be empty.",
                nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_query_client_connect(_handle,
            endpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public RegistryTopologyEntry[] Snapshot()
    {
        EnsureNotDisposed();
        return ReadTopologyEntries(_handle, IntPtr.Zero,
            NativeMethods.zlink_registry_query_snapshot);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_registry_query_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    ~RegistryQueryClient()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(RegistryQueryClient));
    }

    internal static RegistryTopologyEntry[] ReadTopologyEntries(IntPtr handle,
        IntPtr filterPtr, TopologyReadFn nativeCall)
    {
        nuint count = 0;
        int rc = nativeCall(handle, filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryTopologyEntry>();

        int entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = nativeCall(handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowIfError(rc);

            RegistryTopologyEntry[] result = new RegistryTopologyEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkRegistryTopologyEntry native =
                    Marshal.PtrToStructure<ZlinkRegistryTopologyEntry>(current);
                result[i] = RegistryTopologyEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    internal delegate int TopologyReadFn(IntPtr handle, IntPtr filter,
        IntPtr entries, ref nuint count);
}
