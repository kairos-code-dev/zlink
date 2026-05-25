namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorContext
{
    string ActorId { get; }

    string? SessionId { get; }

    string? SpotName { get; }

    RoutingId? SpotRid { get; }

    bool IsJoined { get; }

    IZLinkBoundSession BoundSession { get; }

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
}

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default);
}
