using System;
using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

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

    // Pre-encoded channel-name interop for the data plane: callers pin a cached
    // UTF-8 buffer once instead of re-marshalling the managed string on every
    // part, mirroring zlink_spot_publish_part_utf8.
    [DllImport(LibraryName, EntryPoint = "zlink_spot_send_channel_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_send_channel_part_utf8(
        IntPtr spot, byte* channelName, ref ZlinkMsg part, int flags,
        ZlinkPartFlag partFlag);

    [DllImport(LibraryName, EntryPoint = "zlink_spot_request_channel_part",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern unsafe int zlink_spot_request_channel_part_utf8(
        IntPtr spot, byte* channelName, ref ZlinkMsg part, IntPtr handler,
        IntPtr userData, int flags, ZlinkPartFlag partFlag, uint timeoutMs);

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
