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

public interface IZLinkSessionIdentityContext
{
    string SessionId { get; }

    RoutingId? RoutingId { get; }

    string? LocalAddr { get; }

    string? RemoteAddr { get; }
}

public interface IZLinkSessionChannelClient
{
    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);
}

public interface IZLinkSessionClientStream
{
    IZLinkSessionSendCall Send<TMessage>(TMessage message);

    IZLinkSessionReplyCall Reply<TMessage>(TMessage message);
}

public interface IZLinkSessionActorDispatchContext
{
    ValueTask<IZLinkActorRef> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> CreateRemoteActorAsync(
        RoutingId actorNodeId,
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    IZLinkSessionRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchToActorAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        ZlinkStreamHeader header,
        Message body,
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

    ValueTask DisconnectActorAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkSessionContext :
    IZLinkSessionIdentityContext,
    IZLinkSessionChannelClient,
    IZLinkSessionClientStream,
    IZLinkSessionActorDispatchContext,
    IZLinkSessionLifecycle;

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
