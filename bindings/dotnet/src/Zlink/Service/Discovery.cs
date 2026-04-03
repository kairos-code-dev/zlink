// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Discovery : IDisposable
{
    private const ServiceMonitorEvents DefaultMonitorEvents =
        ServiceMonitorEvents.Error
        | ServiceMonitorEvents.DiscoveryServiceUp
        | ServiceMonitorEvents.DiscoveryServiceDown
        | ServiceMonitorEvents.DiscoveryProvidersChanged
        | ServiceMonitorEvents.Closed;
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

    public ServiceMonitor MonitorOpen(
        ServiceMonitorEvents events = DefaultMonitorEvents)
    {
        EnsureNotDisposed();
        EnumValidation.EnsureServiceMonitorEvents(events, nameof(events));
        var options = new ZlinkServiceMonitorOpenOptions
        {
            Events = (uint)events
        };
        IntPtr monitor = NativeMethods.zlink_service_monitor_open(_handle,
            in options);
        if (monitor == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new ServiceMonitor(monitor);
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
