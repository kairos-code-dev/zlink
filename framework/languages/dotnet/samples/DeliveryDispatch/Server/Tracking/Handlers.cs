using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.Tracking;

[ZLinkHandlerGroup(SampleNames.TrackingRouteChannel)]
internal sealed class DeliveryStatusChangedHandler(
    EvidenceStore evidence,
    IZLinkChannelClient channels,
    ILogger<DeliveryStatusChangedHandler> logger)
    : IZLinkRequestHandler<DeliveryStatusChanged, DeliveryStatusAck>
{
    public async ValueTask<DeliveryStatusAck> HandleAsync(
        DeliveryStatusChanged request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        evidence.Append(request);
        _ = await channels.RequestToChannel(SampleNames.CustomerRouteChannel, request)
            .Async<DeliveryStatusAck>(cancellationToken);
        logger.LogInformation(
            "deliverydispatch tracking: status delivery={DeliveryId} status={Status} courier={CourierId}",
            request.DeliveryId,
            request.Status,
            request.CourierId);
        return new DeliveryStatusAck(request.DeliveryId, request.Status);
    }
}
