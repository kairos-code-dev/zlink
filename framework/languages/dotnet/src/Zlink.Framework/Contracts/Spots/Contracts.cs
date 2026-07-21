namespace Zlink.Framework.Contracts.Spots;

public enum ZLinkSpotCreateState
{
    Existing = 0,
    Created = 1,
    Rejected = 2
}

internal readonly record struct ZLinkSpotAcceptRejectResult(bool Accepted, ZLinkMessage? Reply)
{
    public static ZLinkSpotAcceptRejectResult Accept(ZLinkMessage? reply = null)
    {
        return new ZLinkSpotAcceptRejectResult(true, reply);
    }

    public static ZLinkSpotAcceptRejectResult Accept<TReply>(TReply reply)
    {
        return Accept(ZLinkMessage.From(reply));
    }

    public static ZLinkSpotAcceptRejectResult Reject(ZLinkMessage? reply = null)
    {
        return new ZLinkSpotAcceptRejectResult(false, reply);
    }

    public static ZLinkSpotAcceptRejectResult Reject<TReply>(TReply reply)
    {
        return Reject(ZLinkMessage.From(reply));
    }
}

public readonly record struct ZLinkSpotCreateResponse(bool Accepted, ZLinkMessage? Reply)
{
    public static ZLinkSpotCreateResponse Accept(ZLinkMessage? reply = null)
    {
        var result = ZLinkSpotAcceptRejectResult.Accept(reply);
        return new ZLinkSpotCreateResponse(result.Accepted, result.Reply);
    }

    public static ZLinkSpotCreateResponse Accept<TReply>(TReply reply)
    {
        var result = ZLinkSpotAcceptRejectResult.Accept(reply);
        return new ZLinkSpotCreateResponse(result.Accepted, result.Reply);
    }

    public static ZLinkSpotCreateResponse Reject(ZLinkMessage? reply = null)
    {
        var result = ZLinkSpotAcceptRejectResult.Reject(reply);
        return new ZLinkSpotCreateResponse(result.Accepted, result.Reply);
    }

    public static ZLinkSpotCreateResponse Reject<TReply>(TReply reply)
    {
        var result = ZLinkSpotAcceptRejectResult.Reject(reply);
        return new ZLinkSpotCreateResponse(result.Accepted, result.Reply);
    }
}

public readonly record struct ZLinkSpotCreateResult(
    RoutingId SpotRid,
    ZLinkSpotCreateState State,
    ZLinkMessage? Reply);

public readonly record struct ZLinkSpotInfo(
    RoutingId SpotRid);

public interface IZLinkSpotManager
{
    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot, TRequest>(
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return CreateAsync<TSpot>(ZLinkMessage.From(request), cancellationToken);
    }

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot, TRequest>(
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return GetOrCreateAsync<TSpot>(spotRid, ZLinkMessage.From(request), cancellationToken);
    }

    ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot;

    ValueTask<ZLinkSpotInfo?> FindAsync(RoutingId spotRid,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default);

    ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotPublisherClient
{
    IZLinkPublishCall Publish<TEvent>(
        string meshName,
        string channelName,
        string topic,
        TEvent message);
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
