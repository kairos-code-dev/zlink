namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorRef(
    string actorId,
    string actorType,
    ActorRef actor,
    ZLinkActorRemoteAddress remoteAddress,
    bool isRemote,
    RoutingId sessionRid,
    string bindingToken)
    : IZLinkActorRef
{
    public string ActorId { get; } = actorId;

    public string ActorType { get; } = actorType;

    public ActorRef Actor { get; } = actor;

    public bool IsRemote { get; } = isRemote;

    public ZLinkActorRemoteAddress RemoteAddress { get; } = remoteAddress;

    internal RoutingId SessionRid { get; } = sessionRid;

    internal string BindingToken { get; } = bindingToken;
}
