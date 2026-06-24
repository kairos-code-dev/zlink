namespace Zlink.Framework.Contracts.Actors;

public interface IZLinkActorManager
{
    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> CreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> CreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return CreateAsync(
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }

    ValueTask<ActorRef?> FindAsync(
        string actorId,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> GetOrCreateAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default);

    ValueTask<ActorRef> GetOrCreateAsync<TRequest>(
        string actorId,
        string actorType,
        TRequest createRequest,
        CancellationToken cancellationToken = default)
    {
        return GetOrCreateAsync(
            actorId,
            actorType,
            ZLinkMessage.From(createRequest),
            cancellationToken);
    }
}

public interface IZLinkActorGateway
{
    IZLinkActorJoinSpotCall JoinSpot(
        ActorRef actor,
        RoutingId spotRid,
        ZLinkMessage request);

    IZLinkActorJoinSpotCall JoinSpot<TRequest>(
        ActorRef actor,
        RoutingId spotRid,
        TRequest request)
    {
        return JoinSpot(actor, spotRid, ZLinkMessage.From(request));
    }

    IZLinkActorJoinEntrySpotCall JoinEntrySpot(
        ActorRef actor,
        RoutingId spotNodeRid,
        ZLinkMessage request);

    IZLinkActorJoinEntrySpotCall JoinEntrySpot<TRequest>(
        ActorRef actor,
        RoutingId spotNodeRid,
        TRequest request)
    {
        return JoinEntrySpot(actor, spotNodeRid, ZLinkMessage.From(request));
    }
}
