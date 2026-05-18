namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRef(
    string actorId,
    string actorType,
    string routerChannelId,
    RoutingId targetNodeRid,
    Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync)
    : IZLinkActorRef
{
    public string ActorId { get; } = actorId;

    public string ActorType { get; } = actorType;

    public string RouterChannelId { get; } = routerChannelId;

    public RoutingId TargetNodeRid { get; } = targetNodeRid;

    public ValueTask NotifyDisconnectedAsync(
        CancellationToken cancellationToken = default)
    {
        return notifyDisconnectedAsync(this, cancellationToken);
    }
}
