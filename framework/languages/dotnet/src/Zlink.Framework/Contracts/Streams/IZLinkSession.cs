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
    /// <see cref="IZLinkSessionActor.RelayAsync"/>, but
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

public interface IZLinkSessionActorBindingContext
{
    IReadOnlyCollection<IZLinkSessionActor> BoundActors { get; }

    ValueTask<IZLinkSessionActor> BindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindActorAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindActorAsync(
        IZLinkSessionActor actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? FindActor(string actorId);
}

public interface IZLinkSessionLifecycle
{
    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionClientStream,
    IZLinkSessionActorBindingContext,
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
