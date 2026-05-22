namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRef(
    string actorId,
    string actorType,
    ZLinkActorRemoteAddressState remoteAddressState,
    RoutingId sessionRouterId,
    bool isRemote,
    string bindingToken,
    Func<ZLinkActorRef, CancellationToken, ValueTask> notifyDisconnectedAsync)
    : IZLinkActorRef
{
    public string ActorId { get; } = actorId;

    public string ActorType { get; } = actorType;

    public string RouterChannelId => RemoteAddressSnapshot.RouterChannelId;

    public RoutingId TargetNodeRid => RemoteAddressSnapshot.TargetNodeRid;

    public ulong ActorGeneration => RemoteAddressSnapshot.ActorGeneration;

    public bool IsRemote { get; } = isRemote;

    public ZLinkActorRemoteAddress RemoteAddress => RemoteAddressSnapshot;

    internal RoutingId SessionRouterId { get; } = sessionRouterId;

    internal string BindingToken { get; } = bindingToken;

    internal ZLinkActorRemoteAddress RemoteAddressSnapshot => remoteAddressState.Snapshot;

    internal bool TryUpdateRemoteAddress(
        string routerChannelId,
        RoutingId targetNodeRid,
        ulong expectedActorGeneration,
        ulong newActorGeneration)
    {
        return remoteAddressState.TryUpdate(
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
