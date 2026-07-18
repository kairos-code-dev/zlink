// SPDX-License-Identifier: MPL-2.0

using System.Runtime.InteropServices;
using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

// Shared helpers for MeshNode/Spot/StreamSessionService (parts,count) submits.
internal static class MeshSend
{
    // A (parts, count, metadata) submitter. metadata is a pointer to a pinned
    // zlink_mesh_metadata_view_t, or IntPtr.Zero when no application metadata is
    // attached.
    internal delegate int NativeMetadataSubmitter(IntPtr parts, nuint count,
        IntPtr metadata);

    // Clones and submits a (parts,count) vector, returning the native submit
    // result without throwing on backpressure. Ownership follows the standard
    // NativeMessageParts contract (native consumes on success; clones freed).
    internal static SubmitResult Submit(IReadOnlyList<Message> parts,
        string paramName, NativeMessageParts.NativePartVectorSubmitter submit)
    {
        var captured = 0;
        NativeMessageParts.SubmitClonedVector(parts, paramName,
            (nativeParts, partCount) =>
            {
                var rc = submit(nativeParts, partCount);
                captured = rc;
                return rc;
            }, null);
        return (SubmitResult)captured;
    }

    // Submits a (parts,count) vector with optional immutable outbound
    // application metadata. The metadata bytes and their view struct stay pinned
    // across the synchronous native submit.
    internal static SubmitResult SubmitWithMetadata(IReadOnlyList<Message> parts,
        string paramName, ReadOnlyMemory<byte> metadata,
        NativeMetadataSubmitter submit)
    {
        if (metadata.IsEmpty)
            return Submit(parts, paramName,
                (np, count) => submit(np, count, IntPtr.Zero));

        using var pin = metadata.Pin();
        unsafe
        {
            var view = new ZlinkMeshMetadataView
            {
                Data = (IntPtr)pin.Pointer,
                Size = (nuint)metadata.Length
            };
            var viewHandle = GCHandle.Alloc(view, GCHandleType.Pinned);
            try
            {
                var viewPtr = viewHandle.AddrOfPinnedObject();
                return Submit(parts, paramName,
                    (np, count) => submit(np, count, viewPtr));
            }
            finally
            {
                viewHandle.Free();
            }
        }
    }

    internal static uint EncodeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }
}
