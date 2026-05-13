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

    ValueTask Submit(CancellationToken cancellationToken = default);
}

public interface IZLinkActorClientRequestCall
{
    IZLinkActorClientRequestCall WithPacketName(string packetName);

    IZLinkActorClientRequestCall WithMetadata(
        string key,
        string value);

    IZLinkActorClientRequestCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}

public interface IZLinkActorPlayRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    ZLinkSpotId SpotId);

public interface IZLinkActorSessionBindingStore
{
    ValueTask BindSessionAsync(
        ZLinkActorSessionBinding binding,
        CancellationToken cancellationToken);

    ValueTask UnbindSessionAsync(
        ZLinkActorSessionUnbind binding,
        CancellationToken cancellationToken);

    ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
        string actorId,
        CancellationToken cancellationToken);
}

public readonly record struct ZLinkActorRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid);

public readonly record struct ZLinkActorSessionRoute(
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionBinding(
    string ActorId,
    RoutingId SessionRouterId,
    string BindingToken);

public readonly record struct ZLinkActorSessionUnbind(
    string ActorId,
    string BindingToken);
