using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CourierActorNode;

[ZLinkHandlerGroup(SampleNames.CourierActorNodeRouteChannel)]
internal sealed class FindCourierActorRouteHandler(IZLinkActorManager actorManager)
    : IZLinkRouteRequestHandler<FindCourierActorReq, FindCourierActorRes>
{
    public async ValueTask<FindCourierActorRes> HandleAsync(
        FindCourierActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        var actor = await actorManager.FindAsync(request.CourierId, cancellationToken);
        if (actor is null)
        {
            return new FindCourierActorRes(request.CourierId, null);
        }

        var actorRef = actor.Value;
        return new FindCourierActorRes(
            request.CourierId,
            ZLinkActorRefSnapshot.From(actorRef));
    }
}

[ZLinkHandlerGroup(SampleNames.CourierActorNodeRouteChannel)]
internal sealed class EnsureCourierActorRouteHandler(
    IZLinkActorManager actorManager,
    ILogger<EnsureCourierActorRouteHandler> logger)
    : IZLinkRouteRequestHandler<EnsureCourierActorReq, EnsureCourierActorRes>
{
    public async ValueTask<EnsureCourierActorRes> HandleAsync(
        EnsureCourierActorReq request,
        ZLinkRouteRequestContext context,
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
            ZLinkActorRefSnapshot.From(actor));
    }
}

[ZLinkHandlerGroup(SampleNames.CourierActorNodeRouteChannel)]
internal sealed class OfferDeliveryRouteHandler(
    IZLinkActorManager actorManager,
    ActorDirectory actors)
    : IZLinkRouteRequestHandler<OfferDeliveryReq, OfferDeliveryRes>
{
    public async ValueTask<OfferDeliveryRes> HandleAsync(
        OfferDeliveryReq request,
        ZLinkRouteRequestContext context,
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
