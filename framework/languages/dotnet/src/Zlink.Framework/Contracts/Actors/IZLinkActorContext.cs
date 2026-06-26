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
    ActorRef Actor,
    ZLinkMessage Reply);

public sealed record ZLinkActorJoinResult<TReply>(
    bool Accepted,
    ActorRef Actor,
    TReply Reply);

public interface IZLinkActorJoinSpotCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);

    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorJoinResult> YieldAsync(
        CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        throw new NotSupportedException("YieldAsync is not supported by this actor join call.");
    }

    async ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Async(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }

    async ValueTask<ZLinkActorJoinResult<TReply>> YieldAsync<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await YieldAsync(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }
}

public interface IZLinkActorJoinEntrySpotCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);

    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorJoinResult> YieldAsync(
        CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;
        throw new NotSupportedException("YieldAsync is not supported by this actor join call.");
    }

    async ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Async(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }

    async ValueTask<ZLinkActorJoinResult<TReply>> YieldAsync<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await YieldAsync(cancellationToken).ConfigureAwait(false);
        return new ZLinkActorJoinResult<TReply>(
            result.Accepted,
            result.Actor,
            result.Reply.Decode<TReply>());
    }
}
