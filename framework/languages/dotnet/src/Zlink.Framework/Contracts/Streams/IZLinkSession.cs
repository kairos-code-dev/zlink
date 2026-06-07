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

public interface IZLinkSessionClient
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActors
{
    IReadOnlyCollection<IZLinkSessionActor> Bound { get; }

    ValueTask<IZLinkSessionActor> BindAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSessionActor> BindAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default);

    IZLinkSessionActor? Find(string actorId);
}

public interface IZLinkSessionContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    IZLinkSessionClient Client { get; }

    IZLinkSessionActors Actors { get; }

    ValueTask CloseAsync();
}

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall Metadata(string key, string value);

    IZLinkSessionSendCall PacketName(string messageName);

    IZLinkSessionSendCall Compress();

    ValueTask Submit();
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall Metadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    ValueTask Submit();
}
