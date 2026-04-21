// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Zlink.Native;

namespace Zlink;

public sealed class RegistryQueryClient : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public RegistryQueryClient(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_registry_query_client_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    public void Connect(string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_query_client_connect(_handle,
            endpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public RegistryTopologyEntry[] Snapshot(RegistryTopologyFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkRegistryTopologyFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            if (filter != null)
            {
                RegistryTopologyFilter value = filter;
                if (value.ServiceKind.HasValue || value.ServiceRole.HasValue
                    || !string.IsNullOrEmpty(value.ServiceName)
                    || value.RoutingId.HasValue || value.State.HasValue
                    || value.Source.HasValue)
                {
                    nativeFilter.ServiceKind =
                        (int)value.ServiceKind.GetValueOrDefault();
                    nativeFilter.ServiceRole =
                        (ushort)value.ServiceRole.GetValueOrDefault();
                    nativeFilter.State = (int)value.State.GetValueOrDefault();
                    nativeFilter.Source = (int)value.Source.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.ServiceName))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.ServiceName,
                            nameof(RegistryTopologyFilter.ServiceName));
                        WriteFixedString(value.ServiceName,
                            nativeFilter.ServiceName, 256);
                    }
                    if (value.RoutingId.HasValue)
                    {
                        nativeFilter.RoutingId = NativeHelpers.WriteRoutingId(
                            RoutingIdCodec.FromRoutingId(
                                value.RoutingId.Value));
                    }

                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadTopologyEntries(_handle, filterPtr,
                NativeMethods.zlink_registry_query_snapshot);
        }
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        Destroy(throwOnError: true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~RegistryQueryClient()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_registry_query_destroy(ref handle);
        if (rc == 0)
        {
            _handle = IntPtr.Zero;
            return;
        }

        _handle = originalHandle;
        if (throwOnError)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
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
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryTopologyEntry>();

        int entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = nativeCall(handle, filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

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

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        byte[] encoded = System.Text.Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
        {
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");
        }

        for (int i = 0; i < capacity; i++)
            destination[i] = 0;
        for (int i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }
}
