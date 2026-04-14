// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Zlink;
using Zlink.Native;

namespace Zlink;

public sealed class Discovery : IDisposable, IAsyncDisposable
{
    private IntPtr _handle;

    public Discovery(Context context, ServiceType serviceType, string serviceName)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        BoundaryValidation.ValidateFixedUtf8(serviceName, nameof(serviceName));
        _handle = NativeMethods.zlink_discovery_new(context.Handle,
            (int)serviceType, serviceName);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void ConnectRegistry(string registryPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(registryPubEndpoint,
            nameof(registryPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_connect_registry(_handle,
            registryPubEndpoint);
        ZlinkException.ThrowIfError(rc);
    }

    public void SetValue(long value)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_value(_handle, value);
        ZlinkException.ThrowIfError(rc);
    }

    public long GetValue()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_get_value(_handle, out long value);
        ZlinkException.ThrowIfError(rc);
        return value;
    }

    public void SetMetadata(Message metadata)
    {
        if (metadata == null)
            throw new ArgumentNullException(nameof(metadata));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_metadata(_handle,
            NativeMethods.zlink_msg_data(ref metadata.Handle),
            (nuint)metadata.Size);
        ZlinkException.ThrowIfError(rc);
    }

    public Message GetMetadata()
    {
        EnsureNotDisposed();
        using var metadata = new Message();
        int rc = NativeMethods.zlink_discovery_get_metadata(_handle,
            ref metadata.Handle);
        ZlinkException.ThrowIfError(rc);
        return metadata.Move();
    }

    public MemberPeerEntry[] MemberPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_discovery_member_peers(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_discovery_member_peers(_handle, entries,
                ref actual);
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

    public Message MemberPeerMetadata(ServiceRole serviceRole, string endpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        using var metadata = new Message();
        int rc = NativeMethods.zlink_discovery_member_peer_metadata(_handle,
            (ushort)serviceRole, endpoint, ref metadata.Handle);
        ZlinkException.ThrowIfError(rc);
        return metadata.Move();
    }

    public RoutingId ResolveSpot(RoutingId spotRid)
    {
        EnsureNotDisposed();
        byte[] spotRidBytes = spotRid.ToByteArray();
        unsafe
        {
            fixed (byte* spotRidPtr = spotRidBytes)
            {
                ZlinkRoutingId nativeSpotRid = default;
                if (spotRidBytes.Length != 0)
                {
                    nativeSpotRid = NativeHelpers.WriteRoutingId(
                        new ReadOnlySpan<byte>(spotRidPtr, spotRidBytes.Length));
                }

                int rc = NativeMethods.zlink_discovery_resolve_spot(_handle,
                    ref nativeSpotRid, out ZlinkRoutingId ownerNodeRoutingId);
                ZlinkException.ThrowIfError(rc);
                return RoutingId.FromBytes(
                    NativeHelpers.ReadRoutingId(ref ownerNodeRoutingId));
            }
        }
    }

    public void SetDealerPeerMode(DiscoveryDealerPeerMode mode)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_dealer_peer_mode(_handle,
            (int)mode);
        ZlinkException.ThrowIfError(rc);
    }

    public ServiceMonitor MonitorOpen(params ServiceMonitorEventMask[] events)
    {
        EnsureNotDisposed();
        uint mask = 0;
        if (events == null || events.Length == 0)
        {
            mask = (uint)ServiceMonitorEventMask.All;
        }
        else
        {
            foreach (ServiceMonitorEventMask value in events)
            {
                EnumValidation.EnsureServiceMonitorEvents(value,
                    nameof(events));
                mask |= (uint)value;
            }
        }
        var options = new ZlinkServiceMonitorOpenOptions
        {
            Events = mask
        };
        IntPtr monitor = NativeMethods.zlink_service_monitor_open(_handle,
            in options);
        if (monitor == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new ServiceMonitor(monitor);
    }

    internal ServiceMonitor MonitorOpen(ServiceMonitorEvents events)
    {
        return MonitorOpen((ServiceMonitorEventMask)(uint)events);
    }

    public void Close()
    {
        Dispose();
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;
        NativeMethods.zlink_discovery_destroy(ref _handle);
        _handle = IntPtr.Zero;
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    ~Discovery()
    {
        Dispose();
    }

    private void EnsureNotDisposed()
    {
        if (_handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Discovery));
    }
}
