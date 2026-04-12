using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_poller_new();

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_destroy(ref IntPtr poller);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_size(IntPtr poller,
        out int errorOut);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_add(IntPtr poller, IntPtr socket,
        IntPtr userData, short events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_add_fd(IntPtr poller, int fd,
        IntPtr userData, short events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_modify(IntPtr poller, IntPtr socket,
        short events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_modify_fd(IntPtr poller, int fd,
        short events);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_remove(IntPtr poller, IntPtr socket);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_remove_fd(IntPtr poller, int fd);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poller_wait_all(IntPtr poller,
        [Out] ZlinkPollerEvent[] events, int nEvents, long timeout,
        out int errorOut);

    [DllImport(LibraryName, EntryPoint = "zlink_poll",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poll(
        [In, Out] ZlinkPollItemUnix[] items, int nitems, long timeout,
        out int errorOut);

    [DllImport(LibraryName, EntryPoint = "zlink_poll",
        CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_poll(
        [In, Out] ZlinkPollItemWindows[] items, int nitems, long timeout,
        out int errorOut);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_proxy(IntPtr frontend, IntPtr backend,
        IntPtr capture);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_proxy_steerable(IntPtr frontend,
        IntPtr backend, IntPtr capture, IntPtr control);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_has(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string capability);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_sleep(int seconds);
}
