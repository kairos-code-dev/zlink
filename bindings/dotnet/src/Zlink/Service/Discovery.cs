// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using Zlink;
using Zlink.Native;

namespace Zlink.Service;

public sealed class Discovery : IDisposable
{
    private const uint DefaultMonitorEvents =
        (1u << 0) | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7)
        | (1u << 17);
    private IntPtr _handle;

    public Discovery(Context context, ServiceType serviceType, string serviceName)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        if (serviceName == null)
            throw new ArgumentNullException(nameof(serviceName));
        _handle = NativeMethods.zlink_discovery_new(context.Handle,
            (int)serviceType, serviceName);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.FromLastError();
    }

    internal IntPtr Handle => _handle;

    public void ConnectRegistry(string registryPubEndpoint)
    {
        ValidateNotEmpty(registryPubEndpoint, nameof(registryPubEndpoint));
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

    public Message GetMemberPeerMetadata(ushort serviceRole, string endpoint)
    {
        ValidateNotEmpty(endpoint, nameof(endpoint));
        EnsureNotDisposed();
        using var metadata = new Message();
        int rc = NativeMethods.zlink_discovery_member_peer_metadata(_handle,
            serviceRole, endpoint, ref metadata.Handle);
        ZlinkException.ThrowIfError(rc);
        return metadata.Move();
    }

    public ServiceMonitor OpenMonitor(uint events = DefaultMonitorEvents)
    {
        EnsureNotDisposed();
        var options = new ZlinkServiceMonitorOpenOptions
        {
            Events = events
        };
        IntPtr monitor = NativeMethods.zlink_service_monitor_open(_handle,
            in options);
        if (monitor == IntPtr.Zero)
            throw ZlinkException.FromLastError();
        return new ServiceMonitor(monitor);
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

    private static void ValidateNotEmpty(string value, string paramName)
    {
        if (value == null)
            throw new ArgumentNullException(paramName);
        if (value.Length == 0)
            throw new ArgumentException("Value must not be empty.", paramName);
    }

}
