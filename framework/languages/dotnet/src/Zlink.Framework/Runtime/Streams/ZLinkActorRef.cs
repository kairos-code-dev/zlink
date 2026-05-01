namespace Zlink.Framework.Runtime.Streams;

internal sealed record ZLinkActorRef(
    string ActorId,
    string ActorType,
    string RouterChannelId,
    RoutingId TargetNodeRid) : IZLinkActorRef;
