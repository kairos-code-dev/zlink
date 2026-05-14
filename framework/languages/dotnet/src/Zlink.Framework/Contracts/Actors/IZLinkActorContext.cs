namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorContext
{
    string ActorId { get; }

    string? SessionId { get; }

    string? SpotName { get; }

    RoutingId? SpotRid { get; }

    bool IsJoined { get; }

    void AddPacket<THandler>()
        where THandler : class;

    void AddPacket<THandler>(string messageName)
        where THandler : class;

    IZLinkSpot GetSpot();

    TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        string spotName,
        TRequest request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        ZLinkSpotId spotId,
        TRequest request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request);

    IZLinkRequestCall RequestChannel<TRequest>(
        string channelName,
        TRequest request);

    IZLinkSendCall SendChannel<TMessage>(
        string channelName,
        TMessage message);

    IZLinkActorSendCall Send<TMessage>(TMessage message);

    IZLinkActorReplyCall Reply<TMessage>(TMessage message);

    ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        string spotName,
        TRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        ZLinkSpotId spotId,
        TRequest request,
        CancellationToken cancellationToken = default);

    ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
