using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode;

internal sealed class FindCourierActorRouteHandler(
    IZLinkActorManager actorManager,
    ActorDirectory actors)
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
    ActorDirectory actors,
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

internal sealed class OfferDeliveryRouteHandler(
    IZLinkActorManager actorManager,
    ActorDirectory actors)
    : IZLinkSpotRequestHandler<CourierEntrySpot, OfferDeliveryReq, OfferDeliveryRes>
{
    public async ValueTask<OfferDeliveryRes> HandleAsync(
        CourierEntrySpot spot,
        OfferDeliveryReq request,
        CancellationToken cancellationToken)
    {
        var actorRef = await actorManager.FindAsync(request.CourierId, cancellationToken)
                       ?? throw new InvalidOperationException($"Courier actor is not bound: {request.CourierId}");
        var actor = actors.Require(actorRef.ActorId);
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(SampleTimings.CourierDecisionTimeout);
        try
        {
            return await actor.OfferAsync(request, timeoutSource.Token);
        }
        catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested
            && !cancellationToken.IsCancellationRequested)
        {
            return new OfferDeliveryRes(
                request.DeliveryId,
                request.CourierId,
                false,
                "courier did not respond before dispatch timeout");
        }
    }
}
