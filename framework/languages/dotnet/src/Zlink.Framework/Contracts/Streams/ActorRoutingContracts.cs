namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ZLinkSpotId SpotId);

public interface IZLinkActorSessionBindingStore
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);

    ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);

public readonly record struct ZLinkActorSessionRoute(
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionBinding(
    string ActorId,
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionUnbind(
    string ActorId,
    string BindingToken);
