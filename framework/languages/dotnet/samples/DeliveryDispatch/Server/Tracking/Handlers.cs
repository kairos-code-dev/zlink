using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;

namespace DeliveryDispatch.Server.Tracking;

[ZLinkHandlerGroup(SampleNames.TrackingRouteChannel)]
internal sealed class DeliveryStatusChangedHandler(
    EvidenceStore evidence,
    IZLinkRouteClient routes,
    IZLinkActorLocationResolver actors,
    ILogger<DeliveryStatusChangedHandler> logger)
    : IZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes>
{
    public async ValueTask<DeliveryStatusChangedRes> HandleAsync(
        DeliveryStatusChangedReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        evidence.Append(request);
        var updated = new DeliveryStatusUpdatedMsg(
            request.DeliveryId,
            request.CustomerId,
            request.Status,
            request.CourierId,
            request.OccurredAt);
        var customerEntry = await actors.ResolveActorSpotAddressAsync(
                                SampleNames.CustomerActorType,
                                request.CustomerId,
                                cancellationToken)
                            ?? throw new InvalidOperationException("Customer actor spot is not available.");
        logger.LogInformation(
            "deliverydispatch tracking: customer actor address customer={CustomerId} node={NodeRid} spot={SpotRid}",
            request.CustomerId,
            customerEntry.NodeRid,
            customerEntry.SpotRid);
        routes.SendToSpot(SampleNames.CustomerActorDiscovery, customerEntry, updated)
            .PacketName(nameof(DeliveryStatusUpdatedMsg))
            .Submit(cancellationToken);
        logger.LogInformation(
            "deliverydispatch tracking: status delivery={DeliveryId} status={Status} courier={CourierId}",
            request.DeliveryId,
            request.Status,
            request.CourierId);
        return new DeliveryStatusChangedRes(request.DeliveryId, request.Status);
    }
}
