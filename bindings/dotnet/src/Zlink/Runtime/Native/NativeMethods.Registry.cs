using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_bind(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pubEndpoint,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string routerEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_set(IntPtr registry,
        int option, uint value);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern uint zlink_registry_get(IntPtr registry,
        int option, out int error);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_add_peer(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string peerPubEndpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_status(IntPtr registry,
        out ZlinkRegistryStatus status);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_service_summary(
        IntPtr registry, IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_topology(IntPtr registry,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_member_peers(IntPtr registry,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_registry_query_client_new(IntPtr ctx);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_client_connect(
        IntPtr client,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string endpoint);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_client_topology(IntPtr client,
        IntPtr filter, IntPtr entries, ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_query_client_destroy(
        ref IntPtr client);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_registry_destroy(ref IntPtr registry);
}
