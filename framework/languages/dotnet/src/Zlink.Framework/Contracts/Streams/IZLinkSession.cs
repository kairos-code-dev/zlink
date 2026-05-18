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
    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

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
    IZLinkSessionChannelClient,
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
