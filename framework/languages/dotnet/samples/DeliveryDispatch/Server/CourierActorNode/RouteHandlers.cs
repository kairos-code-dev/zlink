using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode;

internal sealed class FindCourierActorRouteHandler(
    IZLinkActorManager actorManager)
    : IZLinkSpotRequestHandler<CourierEntrySpot, FindCourierActorReq, FindCourierActorRes>
{
    public async ValueTask<FindCourierActorRes> HandleAsync(
        CourierEntrySpot spot,
        FindCourierActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actorManager.FindAsync(request.CourierId, cancellationToken);
        if (actor is null)
        {
            return new FindCourierActorRes(request.CourierId, null);
        }

        return new FindCourierActorRes(
            request.CourierId,
            ActorRefSnapshot.From(actor.Value));
    }
}

internal sealed class EnsureCourierActorRouteHandler(
    IZLinkActorManager actorManager,
    ILogger<EnsureCourierActorRouteHandler> logger)
    : IZLinkSpotRequestHandler<CourierEntrySpot, EnsureCourierActorReq, EnsureCourierActorRes>
{
    public async ValueTask<EnsureCourierActorRes> HandleAsync(
        CourierEntrySpot spot,
        EnsureCourierActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actorManager.GetOrCreateAsync(
            request.CourierId,
            SampleNames.CourierActorType,
            request,
            cancellationToken);
        logger.LogInformation(
            "deliverydispatch courier-route: ensured courier={CourierId} node={NodeRid}",
            request.CourierId,
            actor.NodeRid);
        return new EnsureCourierActorRes(
            request.CourierId,
            ActorRefSnapshot.From(actor));
    }
}

/// <summary>
/// The offer arrives as a one-way send, is handed to the courier actor, and this handler
/// returns — the spot's serial queue is given straight back, so it is not held for the length
/// of a courier's reaction time. The node does not time the offer either: that deadline belongs
/// to the dispatch worker (common sample spec §7.4).
/// </summary>
internal sealed class OfferDeliveryRouteHandler(
    IZLinkActorManager actorManager,
    IZLinkActorClient actors,
    ILogger<OfferDeliveryRouteHandler> logger)
    : IZLinkSpotPacketHandler<CourierEntrySpot, OfferDeliveryMsg>
{
    public async ValueTask HandleAsync(
        CourierEntrySpot spot,
        OfferDeliveryMsg message,
        CancellationToken cancellationToken)
    {
        var actorRef = await actorManager.FindAsync(message.CourierId, cancellationToken)
                       ?? throw new InvalidOperationException(
                           $"Courier actor is not bound: {message.CourierId}");

        await actors.SendToActor(
            SampleNames.MeshName, actorRef, message).SubmitAsync(cancellationToken);
        logger.LogInformation(
            "deliverydispatch courier-route: offered delivery={DeliveryId} courier={CourierId} attempt={Attempt}",
            message.DeliveryId,
            message.CourierId,
            message.Attempt);
    }
}
