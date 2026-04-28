namespace Zlink.Framework.Actors;

public interface IZLinkActorContext
{
    string ActorKey { get; }

    IZLinkStream? Stream { get; }

    IZLinkActorStreamClient? Client { get; }

    ZLinkSpot? Spot { get; }

    ValueTask<TReply> JoinSpotAsync<TRequest, TReply>(
        global::Zlink.RoutingId spotRid,
        TRequest request,
        CancellationToken cancellationToken = default)
        where TRequest : IZLinkRequest<TReply>;
}
