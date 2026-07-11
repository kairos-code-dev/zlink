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
        ZLinkMessage request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        RoutingId spotRid,
        TRequest request)
    {
        return JoinSpot(spotRid, ZLinkMessage.From(request));
    }

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        RoutingId spotNodeRid,
        ZLinkMessage request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        RoutingId spotNodeRid,
        TRequest request)
    {
        return JoinEntrySpot(spotNodeRid, ZLinkMessage.From(request));
    }
}

public sealed record ZLinkActorJoinResult(
    bool Accepted,
    ActorRef? Actor,
    ZLinkMessage Reply);

public sealed record ZLinkActorJoinResult<TReply>(
    bool Accepted,
    ActorRef? Actor,
    TReply Reply);

public interface IZLinkActorJoinCall
{
    /// <summary>
    /// Executes the join as part of the actor handler turn that created this
    /// call. The returned operation must be awaited by that handler and must
    /// not be started from a detached child task.
    /// </summary>
    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);

    async ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Async(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }
}

public interface IZLinkActorYieldJoinCall : IZLinkActorJoinCall
{
    ValueTask<ZLinkActorJoinResult> Yield(
        CancellationToken cancellationToken = default);

    async ValueTask<ZLinkActorJoinResult<TReply>> Yield<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Yield(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }
}

public interface IZLinkActorJoinSpotCall : IZLinkActorYieldJoinCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorYieldJoinCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}
