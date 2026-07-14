namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorContext
{
    RoutingId? SpotRid { get; }

    IZLinkBoundSession BoundSession { get; }

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

public abstract record ZLinkActorJoinResult
{
    private protected ZLinkActorJoinResult()
    {
    }

    public sealed record Accepted(ActorRef Actor, ZLinkMessage Reply) : ZLinkActorJoinResult;

    public sealed record Rejected(ZLinkMessage Reply) : ZLinkActorJoinResult;
}

public abstract record ZLinkActorJoinResult<TReply>
{
    private protected ZLinkActorJoinResult()
    {
    }

    public sealed record Accepted(ActorRef Actor, TReply Reply) : ZLinkActorJoinResult<TReply>;

    public sealed record Rejected(TReply Reply) : ZLinkActorJoinResult<TReply>;
}

public interface IZLinkActorJoinCall
{
    void Submit(CancellationToken cancellationToken = default);

    /// <summary>
    /// Executes the join as part of the actor handler turn that created this
    /// call. The returned operation must be awaited by that handler and must
    /// not be started from a detached child task.
    /// </summary>
    ValueTask<ZLinkActorJoinResult> Async(
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkActorJoinResult> Yield(
        CancellationToken cancellationToken = default);

    async ValueTask<ZLinkActorJoinResult<TReply>> Async<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Async(cancellationToken).ConfigureAwait(false);
        return result switch
        {
            ZLinkActorJoinResult.Accepted accepted =>
                new ZLinkActorJoinResult<TReply>.Accepted(
                    accepted.Actor,
                    accepted.Reply.Decode<TReply>()),
            ZLinkActorJoinResult.Rejected rejected =>
                new ZLinkActorJoinResult<TReply>.Rejected(rejected.Reply.Decode<TReply>()),
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
    }

    async ValueTask<ZLinkActorJoinResult<TReply>> Yield<TReply>(
        CancellationToken cancellationToken = default)
    {
        var result = await Yield(cancellationToken).ConfigureAwait(false);
        return result switch
        {
            ZLinkActorJoinResult.Accepted accepted =>
                new ZLinkActorJoinResult<TReply>.Accepted(
                    accepted.Actor,
                    accepted.Reply.Decode<TReply>()),
            ZLinkActorJoinResult.Rejected rejected =>
                new ZLinkActorJoinResult<TReply>.Rejected(rejected.Reply.Decode<TReply>()),
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
    }
}

public interface IZLinkActorJoinSpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinSpotCall Timeout(TimeSpan timeout);
}

public interface IZLinkActorJoinEntrySpotCall : IZLinkActorJoinCall
{
    IZLinkActorJoinEntrySpotCall Timeout(TimeSpan timeout);
}
