namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkSessionActor(
    ZLinkSessionContext context,
    ActorRef actorRef,
    RoutingId sessionRid,
    string bindingToken)
    : IZLinkSessionActor
{
    private readonly object _disconnectGate = new();
    private Task? _disconnectTask;

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
        return Context.RelayActorRefAsync(this, raw, cancellationToken)
            .EnsureAcceptedAsync(
                "Session Actor relay",
                ZLinkFrameworkErrorKind.ActorRouteNotFound);
    }

    public ValueTask NotifyDisconnectedAsync(CancellationToken cancellationToken = default)
    {
        Task notification;
        lock (_disconnectGate)
        {
            _disconnectTask ??= Context
                .NotifyActorRefDisconnectedAsync(this, CancellationToken.None)
                .AsTask();
            notification = _disconnectTask;
        }

        return cancellationToken.CanBeCanceled
            ? new ValueTask(notification.WaitAsync(cancellationToken))
            : new ValueTask(notification);
    }
}
