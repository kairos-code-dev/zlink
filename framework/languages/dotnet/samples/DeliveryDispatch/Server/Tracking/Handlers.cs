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
    : IZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes>
{
    public async ValueTask<DeliveryStatusChangedRes> HandleAsync(
        DeliveryStatusChangedReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        evidence.Append(request);
        _ = await channels.RequestToChannel(SampleNames.CustomerRouteChannel, request)
            .Async<DeliveryStatusChangedRes>(cancellationToken);
        logger.LogInformation(
            "deliverydispatch tracking: status delivery={DeliveryId} status={Status} courier={CourierId}",
            request.DeliveryId,
            request.Status,
            request.CourierId);
        return new DeliveryStatusChangedRes(request.DeliveryId, request.Status);
    }
}
