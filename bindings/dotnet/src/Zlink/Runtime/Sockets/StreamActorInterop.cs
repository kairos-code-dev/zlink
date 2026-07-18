// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

// ActorRef marshalling helpers shared by the MeshNode/Spot/StreamSession service
// surfaces. Core 10.0.0 owns actor binding through IStreamSessionService, so the
// raw STREAM socket no longer carries actor bind/unbind/relay plumbing.
internal static class ActorInterop
{
    internal static unsafe ActorRef FromNative(ref ZlinkActorRef native)
    {
        fixed (byte* actorIdBytes = native.ActorId)
        {
            // Records without an actor arrive zero-filled, and node-local shapes
            // legitimately omit the NodeRid; map both to the default value
            // instead of rejecting the record.
            var actorId = NativeHelpers.ReadFixedString(actorIdBytes, 256);
            if (actorId.Length == 0)
                return default;
            var nodeRid = RoutingId.FromNative(ref native.NodeRid) ?? default;
            return new ActorRef(nodeRid, actorId, native.Generation);
        }
    }

    internal static unsafe ZlinkActorRef ToNative(ActorRef actor)
    {
        ZlinkActorRef native = default;
        if (!actor.NodeRid.IsEmpty)
            native.NodeRid = actor.NodeRid.ToNative();
        var actorId = native.ActorId;
        NativeHelpers.WriteFixedString(actor.ActorId, actorId, 256);
        native.Generation = actor.Generation;
        return native;
    }
}
