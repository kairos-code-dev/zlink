namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorLocationRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorLocationRoute(
    string RouterChannelId,
    string ActorId,
    RoutingId TargetNodeRid,
    RoutingId CurrentSpotRid,
    ZLinkSpotKind CurrentSpotKind);

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ulong ActorGeneration);
