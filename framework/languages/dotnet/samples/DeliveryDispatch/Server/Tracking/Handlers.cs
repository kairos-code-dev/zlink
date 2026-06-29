using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.Tracking;

[ZLinkHandlerGroup(SampleNames.TrackingRouteChannel)]
internal sealed class DeliveryStatusChangedHandler(
    EvidenceStore evidence,
    IZLinkChannelClient channels)
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
        Console.Error.WriteLine($"deliverydispatch tracking: status delivery={request.DeliveryId} status={request.Status} courier={request.CourierId}");
        return new DeliveryStatusAck(request.DeliveryId, request.Status);
    }
}
