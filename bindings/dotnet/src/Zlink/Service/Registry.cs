// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Registry : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public Registry(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_registry_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(pubEndpoint, nameof(pubEndpoint));
        BoundaryValidation.ValidateFixedUtf8(routerEndpoint,
            nameof(routerEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_bind(_handle, pubEndpoint,
            routerEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetId(uint registryId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_id(_handle, registryId);
        ZlinkException.ThrowIfError(rc);
    }

    public void AddPeer(string peerPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerPubEndpoint,
            nameof(peerPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_add_peer(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_heartbeat(_handle,
            intervalMs, timeoutMs);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_broadcast_interval(_handle,
            intervalMs);
        ZlinkException.ThrowIfError(rc);
    }

    public RegistryStatus StatusSnapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_status_snapshot(_handle,
            out var native);
        ZlinkException.ThrowIfError(rc);
        return RegistryStatus.FromNative(ref native);
    }

    public RegistryServiceSummaryEntry[] ServiceSummarySnapshot(
        RegistryServiceSummaryFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkRegistryServiceSummaryFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            RegistryServiceSummaryFilter? requestedFilter = filter;
            if (requestedFilter.HasValue)
            {
                RegistryServiceSummaryFilter value = requestedFilter.Value;
                if (value.ServiceKind.HasValue || value.ServiceRole.HasValue
                    || !string.IsNullOrEmpty(value.ServiceName))
                {
                    nativeFilter.ServiceKind =
                        (int)value.ServiceKind.GetValueOrDefault();
                    nativeFilter.ServiceRole =
                        (ushort)value.ServiceRole.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.ServiceName))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.ServiceName,
                            nameof(RegistryServiceSummaryFilter.ServiceName));
                        WriteFixedString(value.ServiceName,
                            nativeFilter.ServiceName,
                            256);
                    }
                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadSummaryEntries(filterPtr);
        }
    }

    public RegistryTopologyEntry[] TopologySnapshot()
    {
        EnsureNotDisposed();
        return ReadTopologyEntries(IntPtr.Zero, true);
    }

    public RegistryTopologyEntry[] TopologyQuery(
        RegistryTopologyFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkRegistryTopologyFilter nativeFilter = default;
            IntPtr filterPtr = IntPtr.Zero;
            RegistryTopologyFilter? requestedFilter = filter;
            if (requestedFilter.HasValue)
            {
                RegistryTopologyFilter value = requestedFilter.Value;
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
                            nativeFilter.ServiceName,
                            256);
                    }
                    if (value.RoutingId.HasValue)
                    {
                        nativeFilter.RoutingId = NativeHelpers.WriteRoutingId(
                            RoutingIdCodec.FromPublicString(
                                value.RoutingId.Value.Value,
                                nameof(RegistryTopologyFilter.RoutingId)));
                    }
                    filterPtr = (IntPtr)(&nativeFilter);
                }
            }

            return ReadTopologyEntries(filterPtr, false);
        }
    }

    public MemberPeerEntry[] MemberPeers(ServiceType serviceType,
        string serviceName)
    {
        BoundaryValidation.ValidateFixedUtf8(serviceName, nameof(serviceName));
        EnsureNotDisposed();

        nuint count = 0;
        int rc = NativeMethods.zlink_registry_member_peers(_handle,
            (int)serviceType, serviceName, IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_registry_member_peers(_handle,
                (int)serviceType, serviceName, entries, ref actual);
            ZlinkException.ThrowIfError(rc);

            MemberPeerEntry[] result = new MemberPeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkMemberPeerEntry native =
                    Marshal.PtrToStructure<ZlinkMemberPeerEntry>(current);
                result[i] = MemberPeerEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    public Message MemberPeerMetadata(ServiceType serviceType,
        string serviceName, ServiceRole serviceRole, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(serviceName, nameof(serviceName));
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();

        using var metadata = new Message();
        int rc = NativeMethods.zlink_registry_member_peer_metadata(_handle,
            (int)serviceType, serviceName, (ushort)serviceRole, endpoint,
            ref metadata.Handle);
        ZlinkException.ThrowIfError(rc);
        return metadata.Move();
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_registry_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Registry()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Registry));
    }

    private RegistryServiceSummaryEntry[] ReadSummaryEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_registry_service_summary_snapshot(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryServiceSummaryEntry>();

        int entrySize = Marshal.SizeOf<ZlinkRegistryServiceSummaryEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_registry_service_summary_snapshot(_handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowIfError(rc);

            RegistryServiceSummaryEntry[] result =
                new RegistryServiceSummaryEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkRegistryServiceSummaryEntry native =
                    Marshal.PtrToStructure<ZlinkRegistryServiceSummaryEntry>(current);
                result[i] = RegistryServiceSummaryEntry.FromNative(ref native);
            }
            return result;
        }
        finally
        {
            Marshal.FreeHGlobal(entries);
        }
    }

    private RegistryTopologyEntry[] ReadTopologyEntries(IntPtr filterPtr,
        bool snapshot)
    {
        nuint count = 0;
        int rc = snapshot
            ? NativeMethods.zlink_registry_topology_snapshot(_handle,
                IntPtr.Zero, ref count)
            : NativeMethods.zlink_registry_topology_query(_handle, filterPtr,
                IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryTopologyEntry>();

        int entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = snapshot
                ? NativeMethods.zlink_registry_topology_snapshot(_handle,
                    entries, ref actual)
                : NativeMethods.zlink_registry_topology_query(_handle, filterPtr,
                    entries, ref actual);
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

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        byte[] encoded = Encoding.UTF8.GetBytes(value);
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
