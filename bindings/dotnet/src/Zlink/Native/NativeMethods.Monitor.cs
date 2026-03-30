using System;
using System.Runtime.InteropServices;

namespace Zlink.Native;

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkMonitorHandlerDelegate(
        ref ZlinkMonitorEvent @event, IntPtr userData);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate void ZlinkServiceMonitorHandlerDelegate(
        ref ZlinkServiceEvent @event, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_socket_monitor_open(
        IntPtr socket, in ZlinkSocketMonitorOpenOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_socket_monitor_open(
        IntPtr socket, IntPtr options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_monitor_handler(IntPtr monitor,
        ZlinkMonitorHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_socket_monitor_recv(IntPtr monitor,
        out ZlinkMonitorEvent @event);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_try_socket_monitor_recv(IntPtr monitor,
        out ZlinkMonitorEvent @event);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void zlink_monitor_ignore_handler(
        ref ZlinkMonitorEvent @event, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_monitor_snapshot(IntPtr monitor,
        out ZlinkMonitorSnapshot snapshot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_service_monitor_open(IntPtr target,
        in ZlinkServiceMonitorOpenOptions options);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_service_monitor_handler(IntPtr monitor,
        ZlinkServiceMonitorHandlerDelegate handler, IntPtr userData);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_service_monitor_recv(IntPtr monitor,
        out ZlinkServiceEvent @event);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_try_service_monitor_recv(IntPtr monitor,
        out ZlinkServiceEvent @event);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_monitor_close(ref IntPtr monitor);
}
