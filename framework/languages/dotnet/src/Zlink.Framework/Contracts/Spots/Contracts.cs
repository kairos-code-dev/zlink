namespace Zlink.Framework.Contracts.Spots;

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    bool Created);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return GetOrCreateAsync<TSpot>(spotRid, [], cancellationToken);
    }

    ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotClient
{
    IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkRequestCall RequestSpot<TMessage>(
        RoutingId spotRid,
        TMessage request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall RequestChannel<TMessage>(
        string channelName,
        TMessage request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}

public interface IZLinkRoutedSpotClient
{
    IZLinkRoutedSpotChannelClient ViaEgressChannel(
        string localEgressChannelName);

    ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
        ZLinkSpotRemoteAddress remoteAddress,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkSpotRef> BindSpotHandleAsync(
        IZLinkSpotRef spot,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRoutedSpotChannelClient
{
    IZLinkSendCall SendSpot<TMessage>(
        RoutingId spotRid,
        TMessage message);

    IZLinkSendCall SendSpot<TMessage>(
        IZLinkSpotRef spot,
        TMessage message);

    IZLinkRequestCall RequestSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);

    IZLinkRequestCall RequestSpot<TRequest>(
        IZLinkSpotRef spot,
        TRequest request);
}

public interface IZLinkSpotRef
{
    RoutingId SpotRid { get; }

    ZLinkSpotKind SpotKind { get; }

    bool IsRemote { get; }

    ZLinkSpotRemoteAddress RemoteAddress { get; }
}

public interface IZLinkSpotMeshPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkSpotPublisherClient : IZLinkSpotMeshPublisherClient
{
}

public interface IZLinkSpotConnectionManager
{
    ValueTask<IZLinkEndpointConnections> GetRouterAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetClientServerChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSpotMeshPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSpotPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : class
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : class
{
    ValueTask HandleAsync(
        TSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorJoinHandler<TSpot, in TActor, in TRequest, TReply>
    where TSpot : class
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}
