using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot.Handlers;

internal sealed class SubscribeDeliveryActorHandler
    : IZLinkEntrySpotActorRequestHandler<CustomerEntrySpot, CustomerActor, SubscribeDelivery, SubscribeDeliveryAccepted>
{
    public ValueTask<SubscribeDeliveryAccepted> HandleAsync(
        CustomerEntrySpot entrySpot,
        CustomerActor actor,
        ZLinkSpotActorRequestContext context,
        SubscribeDelivery message,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var subscribed = entrySpot.SubscribeCustomer(actor.ActorId, message.DeliveryId);
        return ValueTask.FromResult(new SubscribeDeliveryAccepted(subscribed.DeliveryId));
    }
}
