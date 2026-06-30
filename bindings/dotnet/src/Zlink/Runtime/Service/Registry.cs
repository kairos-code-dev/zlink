// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using System.Text;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Registry : IRegistry
{
    public Registry(Context context)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        Handle = NativeMethods.zlink_registry_new(context.Handle);
        if (Handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    internal IntPtr Handle { get; private set; }

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(pubEndpoint, nameof(pubEndpoint));
        BoundaryValidation.ValidateFixedUtf8(routerEndpoint,
            nameof(routerEndpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_registry_bind(Handle, pubEndpoint,
            routerEndpoint);
        ZlinkException.ThrowBindIfError(rc);
    }

    public void SetId(uint registryId)
    {
        SetRegistryOption(RegistryOption.Id, registryId);
    }

    public void AddPeer(string peerPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(peerPubEndpoint,
            nameof(peerPubEndpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_registry_add_peer(Handle,
            peerPubEndpoint);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetHeartbeat(TimeSpan interval, TimeSpan timeout)
    {
        SetRegistryOption(RegistryOption.HeartbeatIntervalMs,
            EncodeMilliseconds(interval, nameof(interval)));
        SetRegistryOption(RegistryOption.HeartbeatTimeoutMs,
            EncodeMilliseconds(timeout, nameof(timeout)));
    }

    public void SetBroadcastInterval(TimeSpan interval)
    {
        SetRegistryOption(RegistryOption.BroadcastIntervalMs,
            EncodeMilliseconds(interval, nameof(interval)));
    }

    public void SetTlsServer(string certPath, string keyPath,
        bool requireClientCert = false)
    {
        if (certPath == null)
            throw new ArgumentNullException(nameof(certPath));
        if (keyPath == null)
            throw new ArgumentNullException(nameof(keyPath));
        EnsureNotDisposed();

        var rc = NativeMethods.zlink_set_tls_server(Handle, certPath, keyPath,
            requireClientCert ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        if (caCertPath == null)
            throw new ArgumentNullException(nameof(caCertPath));
        if (hostname == null)
            throw new ArgumentNullException(nameof(hostname));
        EnsureNotDisposed();

        var rc = NativeMethods.zlink_set_tls_client(Handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public RegistryStatus Status()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_registry_status(Handle,
            out var native);
        ZlinkException.ThrowConfigIfError(rc);
        return TopologyModelConverters.FromNative(ref native);
    }

    public RegistryServiceSummaryEntry[] ServiceSummary(
        RegistryServiceSummaryFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkRegistryServiceSummaryFilter nativeFilter = default;
            var filterPtr = IntPtr.Zero;
            var requestedFilter = filter;
            if (requestedFilter != null)
            {
                var value = requestedFilter;
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

    public RegistryTopologyEntry[] Topology(
        RegistryTopologyFilter? filter = null)
    {
        EnsureNotDisposed();
        unsafe
        {
            ZlinkRegistryTopologyFilter nativeFilter = default;
            var filterPtr = IntPtr.Zero;
            var requestedFilter = filter;
            if (requestedFilter != null)
            {
                var value = requestedFilter;
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
                        nativeFilter.RoutingId =
                            value.RoutingId.Value.ToNative();
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
        var rc = NativeMethods.zlink_registry_member_peers(Handle,
            channelName, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        var entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        var entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_registry_member_peers(Handle,
                channelName, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result = new MemberPeerEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var current = IntPtr.Add(entries, i * entrySize);
                var native =
                    Marshal.PtrToStructure<ZlinkMemberPeerEntry>(current);
                result[i] = TopologyModelConverters.FromNative(ref native);
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
        Destroy(true);
        GC.SuppressFinalize(this);
    }

    public ValueTask DisposeAsync()
    {
        Dispose();
        return ValueTask.CompletedTask;
    }

    private void SetRegistryOption(RegistryOption option, uint value)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_registry_set(Handle, (int)option, value);
        ZlinkException.ThrowConfigIfError(rc);
    }

    private uint GetRegistryOption(RegistryOption option)
    {
        EnsureNotDisposed();
        var value = NativeMethods.zlink_registry_get(Handle, (int)option,
            out var error);
        ZlinkException.ThrowConfigIfError(error);
        return value;
    }

    public RegistryTopologyEntry[] Topology()
    {
        EnsureNotDisposed();
        return ReadTopologyEntries(IntPtr.Zero, true);
    }

    ~Registry()
    {
        Destroy(false);
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;

        var originalHandle = Handle;
        var handle = Handle;
        var rc = NativeMethods.zlink_registry_destroy(ref handle);
        if (rc == 0)
        {
            Handle = IntPtr.Zero;
            return;
        }

        Handle = originalHandle;
        if (throwOnError)
            throw ZlinkException.CreateCloseException(NativeMethods.zlink_errno());
    }

    private void EnsureNotDisposed()
    {
        if (Handle == IntPtr.Zero)
            throw new ObjectDisposedException(nameof(Registry));
    }

    private static uint EncodeMilliseconds(TimeSpan value, string paramName)
    {
        var millis = value.TotalMilliseconds;
        if (double.IsNaN(millis) || double.IsInfinity(millis)
                                 || millis < 0 || millis > uint.MaxValue)
            throw new ArgumentOutOfRangeException(paramName);
        return (uint)Math.Ceiling(millis);
    }

    private RegistryServiceSummaryEntry[] ReadSummaryEntries(IntPtr filterPtr)
    {
        nuint count = 0;
        var rc = NativeMethods.zlink_registry_service_summary(Handle,
            filterPtr, IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<RegistryServiceSummaryEntry>();

        var entrySize = Marshal.SizeOf<ZlinkRegistryServiceSummaryEntry>();
        var entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_registry_service_summary(Handle,
                filterPtr, entries, ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            var result =
                new RegistryServiceSummaryEntry[(int)actual];
            for (var i = 0; i < result.Length; i++)
            {
                var current = IntPtr.Add(entries, i * entrySize);
                var native =
                    Marshal.PtrToStructure<ZlinkRegistryServiceSummaryEntry>(current);
                result[i] = TopologyModelConverters.FromNative(ref native);
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
        return RegistryTopologyReader.Read(Handle,
            snapshot ? IntPtr.Zero : filterPtr,
            NativeMethods.zlink_registry_topology);
    }

    private static unsafe void WriteFixedString(string value, byte* destination,
        int capacity)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length >= capacity)
            throw new ArgumentOutOfRangeException(nameof(value),
                "UTF-8 value exceeds native fixed buffer capacity.");

        for (var i = 0; i < capacity; i++)
            destination[i] = 0;
        for (var i = 0; i < encoded.Length; i++)
            destination[i] = encoded[i];
    }
}