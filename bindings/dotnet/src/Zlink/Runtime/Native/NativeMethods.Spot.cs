using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_spot_new(IntPtr node);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_destroy(ref IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_actors(IntPtr spot,
        IntPtr entries,
        ref nuint count);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, int flags, ZlinkPartFlag partFlag);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_channel_part(IntPtr spot,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string channelName,
        ref ZlinkMsg part, IntPtr handler, IntPtr userData,
        int flags, ZlinkPartFlag partFlag, uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_set_spot_option(IntPtr spot,
        SpotOption option, IntPtr value, nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_get_spot_option(IntPtr spot,
        SpotOption option, IntPtr value, ref nuint valueSize);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_subscription_at(IntPtr handle,
        nuint index, IntPtr filterOut, ref nuint filterLength,
        out int isPattern);
}
