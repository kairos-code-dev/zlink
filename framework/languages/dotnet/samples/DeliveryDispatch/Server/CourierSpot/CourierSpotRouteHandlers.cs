using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.CourierSpot;

[ZLinkHandlerGroup(SampleNames.CourierSpotRouteChannel)]
internal sealed class EnsureCourierActorRouteHandler(IZLinkActorManager actorManager)
    : IZLinkRouteRequestHandler<EnsureCourierActor, CourierActorEnsured>
{
    public async ValueTask<CourierActorEnsured> HandleAsync(
        EnsureCourierActor request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actorManager.GetOrCreateAsync(
            request.CourierId,
            SampleNames.CourierActorType,
            request,
            cancellationToken);
        Console.Error.WriteLine(
            $"deliverydispatch courier-route: ensured courier={request.CourierId} node={actor.NodeRid}");
        return new CourierActorEnsured(
            request.CourierId,
            new ActorRefSnapshot(actor.NodeRid.ToString(), actor.ActorId, actor.Generation));
    }
}

[ZLinkHandlerGroup(SampleNames.CourierSpotRouteChannel)]
internal sealed class OfferDeliveryRouteHandler(
    IZLinkActorManager actorManager,
    CourierActorDirectory actors)
    : IZLinkRouteRequestHandler<OfferDelivery, OfferDeliveryResult>
{
    public async ValueTask<OfferDeliveryResult> HandleAsync(
        OfferDelivery request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actorRef = await actorManager.GetOrCreateAsync(
            request.CourierId,
            SampleNames.CourierActorType,
            request,
            cancellationToken);
        var actor = actors.Require(actorRef.ActorId);
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(SampleTimings.DispatchTimeout);
        try
        {
            await Task.Yield();
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
