// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/dispatch.h (RouteMesh 10.0.0).
// Pull dispatch: ready index -> claim -> receive batch -> reply.
internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate uint ZlinkMeshReadyHandlerDelegate(IntPtr meshNode,
        uint readyDomains, IntPtr userdata);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_set_ready_handler(IntPtr meshNode,
        ZlinkMeshReadyHandlerDelegate? handler, IntPtr userdata);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_ready_batch_new(nuint recordCapacity);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_ready_batch_reset(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_ready_batch_destroy(ref IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_node_drain_ready(IntPtr meshNode,
        uint domains, IntPtr batch, out uint hasResidue, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_mesh_ready_batch_count(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_ready_batch_data(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_ready_batch_take_claim(IntPtr batch,
        nuint index, out ZlinkMeshClaim claim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_claim_release(ref ZlinkMeshClaim claim);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_receive_batch_new(
        nuint messageCapacity, nuint partCapacity, nuint byteCapacity);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_receive_batch_reset(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_receive_batch_destroy(ref IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_claim_recv_batch(ref ZlinkMeshClaim claim,
        IntPtr batch, out ZlinkMeshReceiveRequirements required, int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_mesh_receive_batch_count(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_receive_batch_data(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern nuint zlink_mesh_receive_batch_part_count(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr zlink_mesh_receive_batch_parts(IntPtr batch);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_receive_batch_retain_message(IntPtr batch,
        nuint recordIndex, IntPtr partsOut, ref nuint partCount);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_mesh_reply(ref ZlinkMeshReplyToken token,
        IntPtr parts, nuint partCount, int flags);
}
