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

    public ValueTask RelayAsync(
        ZLinkMessage payload,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(payload);
        var raw = payload.ToRawMessage(context.Runtime.Registration.Codecs);
        return context.RelayActorRefAsync(this, raw, cancellationToken);
    }

    public ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default)
    {
        return context.NotifyActorRefDisconnectedAsync(this, cancellationToken);
    }
}
