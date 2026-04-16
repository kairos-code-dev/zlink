using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    internal const string LibraryName = "zlink";

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate int ZlinkStreamOnRawDelegate(
        IntPtr routingId,
        IntPtr message,
        IntPtr userdata);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkStreamOnPacketDelegate(
        IntPtr stream,
        IntPtr routingId,
        IntPtr header,
        IntPtr body,
        IntPtr userdata);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal unsafe delegate void ZlinkSpotSubHandlerDelegate(
        byte* topic,
        nuint topicLen,
        IntPtr parts,
        nuint partCount,
        IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkFreeFnDelegate(IntPtr data, IntPtr hint);

    static NativeMethods()
    {
        NativeLibraryLoader.EnsureLoaded();
    }
}
