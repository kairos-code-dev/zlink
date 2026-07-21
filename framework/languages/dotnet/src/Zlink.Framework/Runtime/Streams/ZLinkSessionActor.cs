namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActor(
    ZLinkSessionContext context,
    ActorRef actorRef,
    RoutingId sessionRid,
    string bindingToken,
    bool remoteBindingConfirmed)
    : IZLinkSessionActor
{
    private int _awaitingLocationObservation = remoteBindingConfirmed ? 1 : 0;

    internal ZLinkSessionContext Context { get; } = context;

    internal RoutingId SessionRid { get; } = sessionRid;

    internal string BindingToken { get; } = bindingToken;
    public string ActorId => Ref.ActorId;

    public ActorRef Ref { get; } = actorRef;

    internal bool AwaitingLocationObservation =>
        Volatile.Read(ref _awaitingLocationObservation) != 0;

    internal void MarkLocationObserved() =>
        Interlocked.Exchange(ref _awaitingLocationObservation, 0);

    public ValueTask<ZLinkSubmitResult> RelayAsync(
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
