namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; }

    ValueTask OnConnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    /// <summary>
    /// Handles a framework-owned inbound stream payload.
    /// </summary>
    /// <remarks>
    /// The payload is borrowed for the duration of this callback. Session code
    /// may read it or pass it to framework APIs such as
    /// <see cref="IZLinkSessionActorDispatchContext.RelayToActorAsync"/>, but
    /// must not dispose it or move ownership unless it intentionally keeps a
    /// separate copy past the callback lifetime.
    /// </remarks>
    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        _ = payload;
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}

public interface IZLinkSessionIdentityContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }
}

public interface IZLinkSessionClientStream
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActorDispatchContext
{
    IReadOnlyCollection<IZLinkActorRef> BoundActors { get; }

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        ZLinkActorRemoteAddress remoteAddress,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        IZLinkActorRef actor,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Finds an actor handle already bound to this session by actor id.
    /// </summary>
    bool TryGetBoundActor(
        string actorId,
        out IZLinkActorRef actor);

    /// <summary>
    /// Relays a stream packet to an actor without consuming the caller payload.
    /// </summary>
    /// <remarks>
    /// The framework creates any internal copy needed to cross queues or reach
    /// a remote ActorGateway. The caller remains responsible only for the
    /// lifetime it already owns. For inbound session callbacks that lifetime is
    /// managed by the framework runtime.
    /// </remarks>
    ValueTask RelayToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionLifecycle
{
    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionActorAttachmentContext
{
    ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionClientStream,
    IZLinkSessionActorDispatchContext,
    IZLinkSessionLifecycle;

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall Metadata(string key, string value);

    IZLinkSessionSendCall PacketName(string messageName);

    IZLinkSessionSendCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Metadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    ValueTask Submit(CancellationToken cancellationToken = default);
}
