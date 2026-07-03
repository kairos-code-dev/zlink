using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CustomerGateway.Spots.EntrySpot.Handlers;

internal sealed class DeliveryStatusUpdatedHandler(
    ILogger<DeliveryStatusUpdatedHandler> logger)
    : IZLinkSpotPacketHandler<CustomerEntrySpot, DeliveryStatusUpdatedMsg>
{
    public async ValueTask HandleAsync(
        CustomerEntrySpot spot,
        DeliveryStatusUpdatedMsg message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "deliverydispatch customer-entry: status delivery={DeliveryId} customer={CustomerId} status={Status}",
            message.DeliveryId,
            message.CustomerId,
            message.Status);
        await spot.PushStatusAsync(message, cancellationToken);
    }
}
