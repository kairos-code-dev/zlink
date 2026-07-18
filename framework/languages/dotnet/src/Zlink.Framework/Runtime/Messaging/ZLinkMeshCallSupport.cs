namespace Zlink.Framework.Runtime.Messaging;

// Mesh-plane submits ride the per-node async submit queue, which has no
// synchronous admission verdict yet. Failing fast beats reporting
// Backpressured for a message the queue may still deliver (a duplicate on
// retry). Tracked in 90-implementation-gap §12.36.
internal static class ZLinkMeshCallSupport
{
    public static NotSupportedException TrySubmitPendingSyncAdmission()
    {
        return new NotSupportedException(
            "TrySubmit on mesh-plane calls is pending the synchronous "
            + "admission path (gap 90 §12.36); use SubmitAsync.");
    }

    public static NotSupportedException NodeRouteMetadataPending()
    {
        return new NotSupportedException(
            "Node-direct/channel metadata is pending the router-seam "
            + "threading (gap 90 §12.36); spot-direct and publish calls "
            + "carry metadata today.");
    }
}
