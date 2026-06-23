namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActor(
    ZLinkSessionContext context,
    ActorRef actorRef,
    RoutingId sessionRid,
    string bindingToken)
    : IZLinkSessionActor
{
    public string ActorId => Ref.ActorId;

    public ActorRef Ref { get; } = actorRef;

    internal RoutingId SessionRid { get; } = sessionRid;

    internal string BindingToken { get; } = bindingToken;

    public ValueTask RelayRawAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return context.RelayActorRefAsync(this, header, payload, cancellationToken);
    }

    public ValueTask RelayAsync(
        ZlinkStreamHeader header,
        ZLinkMessage payload,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(payload);
        var raw = payload.ToRawMessage();
        return context.RelayActorRefAsync(this, header, raw, cancellationToken);
    }

    public ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default)
    {
        return context.NotifyActorRefDisconnectedAsync(this, cancellationToken);
    }
}
