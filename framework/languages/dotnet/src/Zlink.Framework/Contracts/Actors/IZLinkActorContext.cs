namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorContext
{
    string ActorId { get; }

    string? SessionId { get; }

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

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid);
}

public sealed record ZLinkActorJoinResult<TReply>(
    int ResultCode,
    ActorRef Actor,
    TReply Reply);

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<ZLinkActorJoinResult<TReply>> SubmitAsync<TReply>(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinEntrySpotCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);

    ValueTask<ActorRef> SubmitAsync(
        CancellationToken cancellationToken = default);
}
