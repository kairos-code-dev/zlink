namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRef(
    string actorId,
    string actorType,
    ZLinkActorRouteState routeState,
    RoutingId sessionRouterId,
    string bindingToken,
    Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync)
    : IZLinkActorRef
{
    public string ActorId { get; } = actorId;

    public string ActorType { get; } = actorType;

    public string RouterChannelId => RouteSnapshot.RouterChannelId;

    public RoutingId TargetNodeRid => RouteSnapshot.TargetNodeRid;

    public ulong ActorGeneration => RouteSnapshot.ActorGeneration;

    internal RoutingId SessionRouterId { get; } = sessionRouterId;

    internal string BindingToken { get; } = bindingToken;

    internal ZLinkActorRoute RouteSnapshot => routeState.Snapshot;

    internal bool TryUpdateRoute(
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration)
    {
        return routeState.TryUpdate(
            routerChannelId,
            targetNodeRid,
            expectedActorGeneration,
            newActorGeneration);
    }

    public ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default)
    {
        return notifyDisconnectedAsync(this, cancellationToken);
    }
}
