namespace Zlink.Framework.Contracts.Actors;

public readonly record struct ZLinkActorRemoteAddress(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ulong ActorGeneration);
