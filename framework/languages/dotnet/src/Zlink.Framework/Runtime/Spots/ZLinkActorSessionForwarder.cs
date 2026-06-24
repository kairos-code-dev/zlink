using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorSessionForwarder
{
    public static bool ShouldForward(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef frameActor,
        out ZLinkBackendActorRef targetActor)
    {
        targetActor = actorState.NativeActorRef ?? frameActor;
        return targetActor.NodeRid != frameActor.NodeRid
            || targetActor.Generation != frameActor.Generation;
    }

    public static void Forward(
        ZLinkFrameworkRuntime runtime,
        ZLinkBackendActorRef targetActor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ZlinkStreamHeader header,
        Message body)
    {
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        using var headerPart = Message.From(headerBytes);
        if (!runtime.ForwardActorBoundSessionPart(
                targetActor,
                sourceNodeRid,
                sourceSessionRid,
                headerPart,
                hasMore: true,
                SendFlags.None))
        {
            throw new InvalidOperationException("Actor session header forward failed.");
        }

        if (!runtime.ForwardActorBoundSessionPart(
                targetActor,
                sourceNodeRid,
                sourceSessionRid,
                body,
                hasMore: false,
                SendFlags.None))
        {
            throw new InvalidOperationException("Actor session body forward failed.");
        }
    }
}
