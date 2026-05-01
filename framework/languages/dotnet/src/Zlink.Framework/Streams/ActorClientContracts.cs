namespace Zlink.Framework.Streams;

public interface IZLinkActorClient
{
    IZLinkActorClientSendCall Send<TMessage>(
        string actorId,
        TMessage message);

    IZLinkActorClientRequestCall Request<TRequest>(
        string actorId,
        TRequest request);
}

public interface IZLinkActorClientSendCall
{
    IZLinkActorClientSendCall WithPacketName(string packetName);

    IZLinkActorClientSendCall WithMetadata(
        string key,
        string value);

    ValueTask Async(CancellationToken cancellationToken = default);
}

public interface IZLinkActorClientRequestCall
{
    IZLinkActorClientRequestCall WithPacketName(string packetName);

    IZLinkActorClientRequestCall WithMetadata(
        string key,
        string value);

    IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkActorSessionRouteResolver
{
    ValueTask<ZLinkActorSessionRoute> ResolveSessionRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkActorSessionLocationWriter
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);

public readonly record struct ZLinkActorSessionRoute(
    string RouterChannelId,
    RoutingId SessionRouterId,
    string SessionId,
    string BindingToken);

public readonly record struct ZLinkActorSessionBinding(
    string ActorId,
    string RouterChannelId,
    RoutingId SessionRouterId,
    string SessionId,
    string BindingToken);

public readonly record struct ZLinkActorSessionUnbind(
    string ActorId,
    string SessionId,
    string BindingToken);
