namespace Zlink.Framework.Streams;

public interface IZLinkSession
{
    IZLinkSessionContext Context { get; set; }

    ValueTask OnConnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnDisconnectedAsync(
        CancellationToken cancellationToken);

    ValueTask OnErrorAsync(
        ZLinkStreamError error,
        CancellationToken cancellationToken);

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken);
}

public interface IZLinkSessionContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);

    ValueTask BindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask UnbindActorAsync(
        CancellationToken cancellationToken = default);

    IZLinkActorRelay OpenActorRelay(
        string routerChannelId,
        RoutingId targetPlayNodeRid,
        string actorId);

    IZLinkSessionRequestCall Request<TRequest>(TRequest request);

    ValueTask CloseAsync(
        CancellationToken cancellationToken = default);

    ValueTask AttachActorAsync(
        IZLinkActor actor,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionSendCall
{
    IZLinkSessionSendCall WithMetadata(string key, string value);

    IZLinkSessionSendCall WithPacketName(string messageName);

    IZLinkSessionSendCall Compress();

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkSessionReplyCall
{
    IZLinkSessionReplyCall WithMetadata(string key, string value);

    IZLinkSessionReplyCall Compress();

    ValueTask Async(CancellationToken cancellationToken = default);
}
