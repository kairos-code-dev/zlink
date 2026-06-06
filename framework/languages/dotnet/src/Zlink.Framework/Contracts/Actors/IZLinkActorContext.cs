namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorContext
{
    RoutingId? SpotRid { get; }

    bool IsJoined { get; }

    IZLinkBoundSession BoundSession { get; }

    IZLinkSpot GetSpot();

    TSpot GetSpot<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkActorJoinSpotCall JoinSpot(
        RoutingId spotRid,
        Message request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid);
}

public sealed record ZLinkActorJoinResult(
    bool Accepted,
    ActorRef Actor,
    Message Reply);

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<ZLinkActorJoinResult> SubmitAsync(
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorJoinEntrySpotCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);

    ValueTask<ActorRef> SubmitAsync(
        CancellationToken cancellationToken = default);
}
