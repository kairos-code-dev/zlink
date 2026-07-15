namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActor(
    ZLinkSessionContext context,
    ActorRef actorRef,
    RoutingId sessionRid,
    string bindingToken)
    : IZLinkSessionActor
{
    internal ZLinkSessionContext Context { get; } = context;

    internal RoutingId SessionRid { get; } = sessionRid;

    internal string BindingToken { get; } = bindingToken;
    public string ActorId => Ref.ActorId;

    public ActorRef Ref { get; } = actorRef;

    public ValueTask RelayAsync(
        ZLinkMessage payload,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(payload);
        var raw = payload.ToRawMessage(Context.Runtime.Registration.Codecs);
        return Context.RelayActorRefAsync(this, raw, cancellationToken);
    }

    public ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default)
    {
        return Context.NotifyActorRefDisconnectedAsync(this, cancellationToken);
    }
}
