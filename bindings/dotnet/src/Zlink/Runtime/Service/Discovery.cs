// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

internal sealed class Discovery : IDiscovery
{
    public Discovery(Context context, AutoConnectType autoConnectType,
        string channelName)
    {
        if (context == null)
            throw new ArgumentNullException(nameof(context));
        BoundaryValidation.ValidateFixedUtf8(channelName, nameof(channelName));
        Handle = NativeMethods.zlink_discovery_new(context.Handle,
            (int)autoConnectType, channelName);
        if (Handle == IntPtr.Zero)
            throw ZlinkException.CreateConfigException(
                NativeMethods.zlink_errno());
    }

    internal IntPtr Handle { get; private set; }

    public int RouteValueMaxSize => GetOption(SocketOption.RouteValueMaxSize);

    public void ConnectRegistry(string registryPubEndpoint)
    {
        BoundaryValidation.ValidateFixedUtf8(registryPubEndpoint,
            nameof(registryPubEndpoint));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_discovery_connect_registry(Handle,
            registryPubEndpoint);
        ZlinkException.ThrowConnectIfError(rc);
    }

    public void SetTlsClient(string caCertPath, string hostname,
        bool trustSystem = false)
    {
        BoundaryValidation.ValidateFixedUtf8(caCertPath, nameof(caCertPath));
        BoundaryValidation.ValidateFixedUtf8(hostname, nameof(hostname));
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_set_tls_client(Handle, caCertPath,
            hostname, trustSystem ? 1 : 0);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public void SetValue(long value)
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_discovery_set_value(Handle, value);
        ZlinkException.ThrowConfigIfError(rc);
    }

    public long GetValue()
    {
        EnsureNotDisposed();
        var rc = NativeMethods.zlink_discovery_get_value(Handle, out var value);
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

    public MemberPeerEntry[] MemberPeers()
    {
        EnsureNotDisposed();
        nuint count = 0;
        var rc = NativeMethods.zlink_discovery_member_peers(Handle,
            IntPtr.Zero, ref count);
        ZlinkException.ThrowConfigIfError(rc);
        if (count == 0)
            return Array.Empty<MemberPeerEntry>();

        var entrySize = Marshal.SizeOf<ZlinkMemberPeerEntry>();
        var entries = Marshal.AllocHGlobal(checked((int)(count * (nuint)entrySize)));
        try
        {
            var actual = count;
            rc = NativeMethods.zlink_discovery_member_peers(Handle, entries,
                ref actual);
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

    public SpotRoute ResolveSpot(RoutingId spotRid)
    {
        EnsureNotDisposed();
        var nativeSpotRid = spotRid.ToNative();
        var rc = NativeMethods.zlink_discovery_resolve_spot(Handle,
            ref nativeSpotRid, out var route);
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
        var rc = NativeMethods.zlink_discovery_resolve_actor(Handle, actorId,
            out var route);
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
            var rc = NativeMethods.zlink_discovery_bind_route(
                Handle, kind, keyPtr, (nuint)key.Length, valuePtr,
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
            var rc = NativeMethods.zlink_discovery_unbind_route(
                Handle, kind, keyPtr, (nuint)key.Length);
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
            var rc = NativeMethods.zlink_discovery_resolve_route(
                Handle, kind, keyPtr, (nuint)key.Length, out owner, ref value);
            ZlinkException.ThrowConfigIfError(rc);
            return new DiscoveryRoute(
                RoutingId.From(NativeHelpers.ReadRoutingId(ref owner)),
                Message.AdoptNative(ref value));
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

    private unsafe int GetOption(SocketOption option)
    {
        EnsureNotDisposed();
        var value = 0;
        var size = (nuint)sizeof(int);
        var rc = NativeMethods.zlink_get_option(Handle, (int)option,
            (IntPtr)(&value), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return value;
    }

    private unsafe void SetSpotOwnerSyncEnabled(bool enabled)
    {
        EnsureNotDisposed();
        var raw = enabled ? 1 : 0;
        var rc = NativeMethods.zlink_set_option(Handle,
            (int)SocketOption.DiscoverySpotOwnerSync, (IntPtr)(&raw),
            sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    private unsafe bool GetSpotOwnerSyncEnabled()
    {
        EnsureNotDisposed();
        var raw = 0;
        var size = (nuint)sizeof(int);
        var rc = NativeMethods.zlink_get_option(Handle,
            (int)SocketOption.DiscoverySpotOwnerSync, (IntPtr)(&raw), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return raw != 0;
    }

    private unsafe void SetActorRouteSyncEnabled(bool enabled)
    {
        EnsureNotDisposed();
        var raw = enabled ? 1 : 0;
        var rc = NativeMethods.zlink_set_option(Handle,
            (int)SocketOption.DiscoveryActorRouteSync, (IntPtr)(&raw),
            sizeof(int));
        ZlinkException.ThrowConfigIfError(rc);
    }

    private unsafe bool GetActorRouteSyncEnabled()
    {
        EnsureNotDisposed();
        var raw = 0;
        var size = (nuint)sizeof(int);
        var rc = NativeMethods.zlink_get_option(Handle,
            (int)SocketOption.DiscoveryActorRouteSync, (IntPtr)(&raw), ref size);
        ZlinkException.ThrowConfigIfError(rc);
        return raw != 0;
    }

    ~Discovery()
    {
        Destroy(false);
    }

    private void Destroy(bool throwOnError)
    {
        if (Handle == IntPtr.Zero)
            return;

        var originalHandle = Handle;
        var handle = Handle;
        var rc = NativeMethods.zlink_discovery_destroy(ref handle);
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