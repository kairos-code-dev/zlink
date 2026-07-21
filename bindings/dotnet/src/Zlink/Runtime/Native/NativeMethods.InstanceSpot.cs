// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;

namespace Systems.Zlink.Runtime.Native;

// P/Invoke surface for core/include/zlink/service/instance_spot_driver.h.
internal static partial class NativeMethods
{
    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_send_to_instance_placement(
        IntPtr spot,
        IntPtr placement,
        IntPtr metadata,
        IntPtr parts,
        nuint partCount,
        int flags);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_spot_request_to_instance_placement(
        IntPtr spot,
        IntPtr placement,
        IntPtr metadata,
        IntPtr parts,
        nuint partCount,
        out ZlinkMeshOperationId operationId,
        int flags,
        uint timeoutMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_activation_claim_owner(
        ref ZlinkInstanceSpotActivationToken token,
        byte[] locationOwnerId,
        nuint locationOwnerIdSize,
        out ZlinkInstanceSpotClaimResult result);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_activation_mark_ready(
        ref ZlinkInstanceSpotActivationToken token,
        uint ownerLeaseValidForMs);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_activation_redirect(
        ref ZlinkInstanceSpotActivationToken token,
        ref ZlinkRoutingId targetNodeRid,
        ref ZlinkRoutingId targetSpotRid,
        ulong targetSpotGeneration);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_activation_abort(
        ref ZlinkInstanceSpotActivationToken token,
        int terminalResult,
        int failureErrno);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_begin_close(IntPtr spot);

    [DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int zlink_instance_spot_renew_owner_admission(
        IntPtr spot,
        uint ownerLeaseValidForMs);
}
