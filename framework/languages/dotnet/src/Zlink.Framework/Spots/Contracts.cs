namespace Zlink.Framework;

public readonly record struct ZLinkSpotCreateResult(
    global::Zlink.RoutingId SpotRid,
    string SpotName,
    bool Created);

public readonly record struct ZLinkSpotInfo(
    global::Zlink.RoutingId SpotRid,
    string SpotName);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkSpotInfo?> GetAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> RemoveAsync(
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotClient
{
    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkRequestCall<TReply> RequestChannel<TReply>(
        string channelName,
        IZLinkRequest<TReply> request);

    IZLinkPublishCall Publish<TEvent>(
        string topic,
        TEvent message);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkSpotConnectionManager
{
    ISpotRouterConnections GetRouter(string spotNodeName);

    ISpotPubSubConnections GetPubSub(string spotNodeName);

    IChannelClientConnections GetChannelClient(
        string spotNodeName,
        string channelName);

    ISpotPublisherConnections GetSpotPublisherClient(
        string spotNodeName,
        string channelName);
}

public interface IZLinkSpotPacketHandler<TSpot, in TMessage>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotRequestHandler<TSpot, in TRequest, TReply>
    where TSpot : ZLinkSpot
{
    ValueTask<TReply> HandleAsync(
        TSpot spot,
        TRequest request,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotSubscriptionHandler<TSpot, in TEvent>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        TEvent message,
        CancellationToken cancellationToken);
}

public interface IZLinkSpotTimerHandler<TSpot>
    where TSpot : ZLinkSpot
{
    ValueTask HandleAsync(
        TSpot spot,
        CancellationToken cancellationToken);
}
