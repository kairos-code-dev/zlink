// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

// Shared helpers for MeshNode/Spot/StreamSessionService (parts,count) submits.
internal static class MeshSend
{
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

    internal static uint EncodeTimeout(TimeSpan timeout)
    {
        return BoundaryValidation.EncodeTimeoutMilliseconds(timeout,
            nameof(timeout));
    }
}
