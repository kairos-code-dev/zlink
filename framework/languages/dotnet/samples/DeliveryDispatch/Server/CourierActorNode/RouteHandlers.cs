using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CourierActorNode;

[ZLinkHandlerGroup(SampleNames.CourierActorNodeRouteChannel)]
internal sealed class EnsureCourierActorRouteHandler(
    IZLinkActorManager actorManager,
    ILogger<EnsureCourierActorRouteHandler> logger)
    : IZLinkRouteRequestHandler<EnsureCourierActor, CourierActorEnsured>
{
    public async ValueTask<CourierActorEnsured> HandleAsync(
        EnsureCourierActor request,
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
        return new CourierActorEnsured(
            request.CourierId,
            new ActorRefSnapshot(actor.NodeRid.ToString(), actor.ActorId, actor.Generation));
    }
}

[ZLinkHandlerGroup(SampleNames.CourierActorNodeRouteChannel)]
internal sealed class OfferDeliveryRouteHandler(
    IZLinkActorManager actorManager,
    ActorDirectory actors)
    : IZLinkRouteRequestHandler<OfferDelivery, OfferDeliveryResult>
{
    public async ValueTask<OfferDeliveryResult> HandleAsync(
        OfferDelivery request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        var actorRef = await actorManager.GetOrCreateAsync(
            request.CourierId,
            SampleNames.CourierActorType,
            request,
            cancellationToken);
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
            return new OfferDeliveryResult(
                request.DeliveryId,
                request.CourierId,
                false,
                "courier did not respond before dispatch timeout");
        }
    }
}
