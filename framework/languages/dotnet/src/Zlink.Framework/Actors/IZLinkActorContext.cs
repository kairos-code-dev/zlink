namespace Zlink.Framework.Actors;

public interface IZLinkActorContext
{
    string ActorId { get; }

    string? SessionId { get; }

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
        RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall WithTimeout(TimeSpan timeout);

    ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default);
}
