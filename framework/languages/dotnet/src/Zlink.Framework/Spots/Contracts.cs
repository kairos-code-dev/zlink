namespace Zlink.Framework.Spots;

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    string SpotName,
    bool Created)
{
    public ZLinkSpotCreateResult(
        ZLinkSpotId spotId,
        string spotName,
        bool created)
        : this(spotId.ToRoutingId(), spotName, created)
    {
    }

    public ZLinkSpotId SpotId => ZLinkSpotId.FromRoutingId(SpotRid);
}

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid,
    string SpotName)
{
    public ZLinkSpotInfo(
        ZLinkSpotId spotId,
        string spotName)
        : this(spotId.ToRoutingId(), spotName)
    {
    }

    public ZLinkSpotId SpotId => ZLinkSpotId.FromRoutingId(SpotRid);
}

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        ZLinkSpotId spotId,
        CancellationToken cancellationToken = default)
    {
        return CreateAsync(spotName, spotId.ToRoutingId(), cancellationToken);
    }

    ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotInfo?> GetAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken = default)
    {
        return GetAsync(spotId.ToRoutingId(), cancellationToken);
    }

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        ZLinkSpotId spotId,
        CancellationToken cancellationToken = default)
    {
        return RemoveAsync(spotId.ToRoutingId(), cancellationToken);
    }
}

public interface IZLinkSpotClient
{
    IZLinkSendCall SendSpot<TMessage>(
        string spotName,
        TMessage message);

    IZLinkSendCall SendSpot<TMessage>(
        ZLinkSpotId spotId,
        TMessage message);

    IZLinkRequestCall RequestSpot<TMessage>(
        string spotName,
        TMessage request);

    IZLinkRequestCall RequestSpot<TMessage>(
        ZLinkSpotId spotId,
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
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : IZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : IZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotActorJoinHandler<TSpot, in TActor, in TRequest, TReply>
    where TSpot : IZLinkSpot
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}
