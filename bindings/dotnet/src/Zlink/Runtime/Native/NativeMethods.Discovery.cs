using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_discovery_new(IntPtr ctx,
        int autoConnectType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_connect_registry(
        IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string registryEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_resolve_spot(IntPtr discovery,
        ref ZlinkRoutingId spotRoutingId, out ZlinkSpotRoute route);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_resolve_actor(IntPtr discovery,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string actorId,
        out ZlinkActorRoute route);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_discovery_bind_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize, byte* value,
        nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_discovery_unbind_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_discovery_resolve_route(
        IntPtr discovery, uint kind, byte* key, nuint keySize,
        out ZlinkRoutingId ownerRoutingId, ref ZlinkMsg value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_set_value(IntPtr discovery,
        long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_get_value(IntPtr discovery,
        out long value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_member_peers(IntPtr discovery,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_discovery_destroy(ref IntPtr discovery);
}