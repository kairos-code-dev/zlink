// SPDX-License-Identifier: MPL-2.0

using System;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using Systems.Zlink;
using Systems.Zlink.Native;

namespace Systems.Zlink;

public sealed class Discovery : IDiscovery
{
    private IntPtr _handle;

    public Discovery(Context context, AutoConnectType autoConnectType,
        string channelName)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        _handle = NativeMethods.zlink_discovery_new(context.Handle,
            (int)autoConnectType, channelName);
        if (_handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    internal IntPtr Handle => _handle;

    public int RouteValueMaxSize => GetOption(SocketOption.RouteValueMaxSize);

    public void ConnectRegistry(string registryPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(registryPubEndpoint,
            nameof(registryPubEndpoint));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_connect_registry(_handle,
            registryPubEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_set_tls_client(_handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetValue(long value)
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_set_value(_handle, value);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public long GetValue()
    {
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_get_value(_handle, out long value);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    private unsafe int GetOption(SocketOption option)
    {
        EnsureNotDisposed();
        int value = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle, (int)option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    public bool SpotOwnerSyncEnabled
    {
        get => GetSpotOwnerSyncEnabled();
        set => SetSpotOwnerSyncEnabled(value);
    }

    public bool ActorRouteSyncEnabled
    {
        get => GetActorRouteSyncEnabled();
        set => SetActorRouteSyncEnabled(value);
    }

    private unsafe void SetSpotOwnerSyncEnabled(bool enabled)
    {
        EnsureNotDisposed();
        int raw = enabled ? 1 : 0;
        int rc = NativeMethods.zlink_set_option(_handle,
            (int)SocketOption.DiscoverySpotOwnerSync, (IntPtr)(&raw),
            (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    private unsafe bool GetSpotOwnerSyncEnabled()
    {
        EnsureNotDisposed();
        int raw = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle,
            (int)SocketOption.DiscoverySpotOwnerSync, (IntPtr)(&raw), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return raw != 0;
    }

    private unsafe void SetActorRouteSyncEnabled(bool enabled)
    {
        EnsureNotDisposed();
        int raw = enabled ? 1 : 0;
        int rc = NativeMethods.zlink_set_option(_handle,
            (int)SocketOption.DiscoveryActorRouteSync, (IntPtr)(&raw),
            (nuint)sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    private unsafe bool GetActorRouteSyncEnabled()
    {
        EnsureNotDisposed();
        int raw = 0;
        nuint size = (nuint)sizeof(int);
        int rc = NativeMethods.zlink_get_option(_handle,
            (int)SocketOption.DiscoveryActorRouteSync, (IntPtr)(&raw), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return raw != 0;
    }

    public MemberPeerEntry[] MemberPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        int rc = NativeMethods.zlink_discovery_member_peers(_handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        int entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        IntPtr entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            nuint actual = count;
            rc = NativeMethods.zlink_discovery_member_peers(_handle, entries,
                ref actual);
            ZlinkException.ThrowConfigIfError(rc);

            MemberPeerEntry[] result = new MemberPeerEntry[(int)actual];
            for (int i = 0; i < result.Length; i++)
            {
                IntPtr current = IntPtr.Add(entries, i * entrySize);
                ZlinkMemberPeerEntry native =
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

    public SpotRoute ResolveSpot(RoutingId spotRid)
    {
        EnsureNotDisposed();
        ZlinkRoutingId nativeSpotRid = spotRid.ToNative();
        int rc = NativeMethods.zlink_discovery_resolve_spot(_handle,
            ref nativeSpotRid, out ZlinkSpotRoute route);
        ZlinkException.ThrowConfigIfError(rc);
        return new SpotRoute(
            RoutingIdInterop.FromNative(ref route.SpotRid)
                ?? throw new ZlinkConfigException(
                    ZlinkConfigException.ErrorCode.InternalError),
            RoutingIdInterop.FromNative(ref route.OwnerNodeRid)
                ?? throw new ZlinkConfigException(
                    ZlinkConfigException.ErrorCode.InternalError),
            (SpotKind)route.SpotKind);
    }

    public ActorRoute ResolveActor(string actorId)
    {
        ActorInterop.ValidateActorId(actorId, nameof(actorId));
        EnsureNotDisposed();
        int rc = NativeMethods.zlink_discovery_resolve_actor(_handle, actorId,
            out ZlinkActorRoute route);
        ZlinkException.ThrowConfigIfError(rc);
        return ActorInterop.FromNative(ref route);
    }

    public unsafe void BindRoute(uint kind, ReadOnlySpan<byte> key,
        ReadOnlySpan<byte> value)
    {
        ValidateRouteKey(kind, key);
        EnsureNotDisposed();
        fixed (byte* keyPtr = key)
        fixed (byte* valuePtr = value)
        {
            int rc = NativeMethods.zlink_discovery_bind_route(
                _handle, kind, keyPtr, (nuint)key.Length, valuePtr,
                (nuint)value.Length);
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    public unsafe void UnbindRoute(uint kind, ReadOnlySpan<byte> key)
    {
        ValidateRouteKey(kind, key);
        EnsureNotDisposed();
        fixed (byte* keyPtr = key)
        {
            int rc = NativeMethods.zlink_discovery_unbind_route(
                _handle, kind, keyPtr, (nuint)key.Length);
            ZlinkException.ThrowConfigIfError(rc);
        }
    }

    public unsafe DiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key)
    {
        ValidateRouteKey(kind, key);
        EnsureNotDisposed();
        fixed (byte* keyPtr = key)
        {
            ZlinkRoutingId owner = default;
            ZlinkMsg value = default;
            int rc = NativeMethods.zlink_discovery_resolve_route(
                _handle, kind, keyPtr, (nuint)key.Length, out owner, ref value);
            ZlinkException.ThrowConfigIfError(rc);
            return new DiscoveryRoute(
                RoutingId.FromBytes(NativeHelpers.ReadRoutingId(ref owner)),
                Message.AdoptNative(ref value));
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

    ~Discovery()
    {
        Destroy(throwOnError: false);
    }

    private void Destroy(bool throwOnError)
    {
        if (_handle == IntPtr.Zero)
            return;

        IntPtr originalHandle = _handle;
        IntPtr handle = _handle;
        int rc = NativeMethods.zlink_discovery_destroy(ref handle);
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
            throw new ObjectDisposedException(nameof(Discovery));
    }

    private static void ValidateRouteKey(uint kind, ReadOnlySpan<byte> key)
    {
        if (kind == DiscoveryRouteKind.Invalid)
            throw new ArgumentOutOfRangeException(nameof(kind));
        if (key.IsEmpty)
            throw new ArgumentException("Route key must not be empty.",
                nameof(key));
    }
}
