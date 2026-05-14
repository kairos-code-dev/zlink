namespace Zlink.Framework.Contracts.Streams;

public interface IZLinkSessionPacket
{
    string PacketName { get; }

    ZLinkMessageMetadata Metadata { get; }

    ZlinkStreamHeader Header { get; }

    Message Body { get; }

    TMessage Decode<TMessage>();
}

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
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken)
    {
        return OnDispatchAsync(packet.Header, packet.Body, cancellationToken);
    }

    ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        _ = header;
        _ = body;
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
    ValueTask<IZLinkActorRef> CreateAndBindActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkActorRef> BindActorHandleAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    IZLinkSessionRequestCall Request<TRequest>(TRequest request);

    ValueTask DispatchToActorAsync(
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default);

    ValueTask DispatchToActorAsync(
        IZLinkActorRef actor,
        IZLinkSessionPacket packet,
        CancellationToken cancellationToken = default);

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
