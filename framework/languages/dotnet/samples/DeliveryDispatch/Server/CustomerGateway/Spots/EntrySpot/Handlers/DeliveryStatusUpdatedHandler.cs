using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot.Handlers;

internal sealed class DeliveryStatusUpdatedHandler(
    ILogger<DeliveryStatusUpdatedHandler> logger)
    : IZLinkEntrySpotActorSendHandler<CustomerEntrySpot, CustomerActor, DeliveryStatusUpdatedMsg>
{
    public async ValueTask HandleAsync(
        CustomerEntrySpot spot,
        CustomerActor actor,
        ZLinkSpotActorSendContext context,
        DeliveryStatusUpdatedMsg message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "deliverydispatch customer-entry: status delivery={DeliveryId} customer={CustomerId} actor={ActorId} status={Status}",
            message.DeliveryId,
            message.CustomerId,
            actor.ActorId,
            message.Status);
        await actor.PushStatusAsync(message, cancellationToken);
        logger.LogInformation(
            "deliverydispatch customer-entry: pushed status delivery={DeliveryId} customer={CustomerId} actor={ActorId} status={Status}",
            message.DeliveryId,
            message.CustomerId,
            actor.ActorId,
            message.Status);
    }
}
