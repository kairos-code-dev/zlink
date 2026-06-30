using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot.Handlers;

internal sealed class SubscribeDeliveryActorHandler
    : IZLinkEntrySpotActorRequestHandler<CustomerEntrySpot, CustomerActor, SubscribeDeliveryReq, SubscribeDeliveryRes>
{
    public ValueTask<SubscribeDeliveryRes> HandleAsync(
        CustomerEntrySpot entrySpot,
        CustomerActor actor,
        ZLinkSpotActorRequestContext context,
        SubscribeDeliveryReq message,
        CancellationToken cancellationToken)
    {
        var subscribed = entrySpot.SubscribeCustomer(actor.ActorId, message.DeliveryId);
        return ValueTask.FromResult(new SubscribeDeliveryRes(subscribed.DeliveryId));
    }
}
