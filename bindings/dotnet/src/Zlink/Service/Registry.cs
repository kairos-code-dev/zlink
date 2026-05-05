// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Zlink;
using Zlink.Native;

namespace Zlink;

public sealed class Registry : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public Registry(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        _handle = NativeMethods.zlink_registry_new(context.Handle);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
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
        ZlinkException.ThrowBindIfError(rc);
    }

    public void SetId(uint registryId)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_id(_handle, registryId);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void AddPeer(string peerPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerPubEndpoint,
            nameof(peerPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_add_peer(_handle,
            peerPubEndpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_heartbeat(_handle,
            intervalMs, timeoutMs);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_set_broadcast_interval(_handle,
            intervalMs);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public RegistryStatus StatusSnapshot()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_registry_status_snapshot(_handle,
            out var native);
        ZlinkException.ThrowConfigIfError(rc);
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
            if (requestedFilter != null)
            {
                RegistryServiceSummaryFilter value = requestedFilter;
                if (value.AutoConnectType.HasValue || value.ServiceRole.HasValue
                    || !string.IsNullOrEmpty(value.ChannelName))
                {
                    nativeFilter.AutoConnectType =
                        (int)value.AutoConnectType.GetValueOrDefault();
                    nativeFilter.ServiceRole =
                        (int)value.ServiceRole.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.ChannelName))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.ChannelName,
                            nameof(RegistryServiceSummaryFilter.ChannelName));
                        WriteFixedString(value.ChannelName,
                            nativeFilter.ChannelName,
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
            if (requestedFilter != null)
            {
                RegistryTopologyFilter value = requestedFilter;
                if (value.AutoConnectType.HasValue
                    || value.ServiceKind.HasValue || value.ServiceRole.HasValue
                    || !string.IsNullOrEmpty(value.ChannelName)
                    || value.RoutingId.HasValue || value.State.HasValue
                    || value.Source.HasValue)
                {
                    nativeFilter.AutoConnectType =
                        (int)value.AutoConnectType.GetValueOrDefault();
                    nativeFilter.ServiceKind =
                        (int)value.ServiceKind.GetValueOrDefault();
                    nativeFilter.ServiceRole =
                        (int)value.ServiceRole.GetValueOrDefault();
                    nativeFilter.State = (int)value.State.GetValueOrDefault();
                    nativeFilter.Source = (int)value.Source.GetValueOrDefault();
                    if (!string.IsNullOrEmpty(value.ChannelName))
                    {
                        BoundaryValidation.ValidateFixedUtf8(value.ChannelName,
                            nameof(RegistryTopologyFilter.ChannelName));
                        WriteFixedString(value.ChannelName,
                            nativeFilter.ChannelName,
                            256);
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

            return ReadTopologyEntries(filterPtr, false);
        }
    }

    public MemberPeerEntry[] MemberPeers(string channelName)
    {
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        EnsureNotDisposed();

        nuint count = 0;
        int rc = NativeMethods.zlink_registry_member_peers(_handle,
            channelName, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_registry_member_peers(_handle,
                channelName, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

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

    ~Registry()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_registry_destroy(ref handle);
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
            throw new ObjectDisposedException(nameof(Registry));
    }

    private RegistryServiceSummaryEntry[] ReadSummaryEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        int rc = NativeMethods.zlink_registry_service_summary_snapshot(_handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryServiceSummaryEntry>();

        int entrySize = Marshal.SizeOf<ZlinkRegistryServiceSummaryEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_registry_service_summary_snapshot(_handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

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
        for (int attempt = 0; attempt < 4; attempt++)
        {
            nuint count = 0;
            int rc = snapshot
                ? NativeMethods.zlink_registry_topology_snapshot(_handle,
                    IntPtr.Zero, ref count)
                : NativeMethods.zlink_registry_topology_query(_handle, filterPtr,
                    IntPtr.Zero, ref count);
            ZlinkException.ThrowConfigIfError(rc);
            if (count == 0)
                return Array.Empty<RegistryTopologyEntry>();

            int entrySize = Marshal.SizeOf<ZlinkRegistryTopologyEntry>();
            IntPtr entries = Marshal.AllocHGlobal(
                checked((int)(count * (nuint)entrySize)));
            try
            {
                nuint actual = count;
                rc = snapshot
                    ? NativeMethods.zlink_registry_topology_snapshot(_handle,
                        entries, ref actual)
                    : NativeMethods.zlink_registry_topology_query(_handle,
                        filterPtr, entries, ref actual);
                if (rc != 0 && IsRetryableSnapshotSizeRace(
                    NativeMethods.zlink_errno()))
                    continue;
                ZlinkException.ThrowConfigIfError(rc);

                RegistryTopologyEntry[] result =
                    new RegistryTopologyEntry[(int)actual];
                for (int i = 0; i < result.Length; i++)
                {
                    IntPtr current = IntPtr.Add(entries, i * entrySize);
                    ZlinkRegistryTopologyEntry native =
                        Marshal.PtrToStructure<ZlinkRegistryTopologyEntry>(
                            current);
                    result[i] = RegistryTopologyEntry.FromNative(ref native);
                }
                return result;
            }
            finally
            {
                Marshal.FreeHGlobal(entries);
            }
        }

        throw ZlinkException.CreateConfigException(NativeMethods.zlink_errno());
    }

    private static bool IsRetryableSnapshotSizeRace(int errno)
        => errno == 105 || errno == 55;

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
