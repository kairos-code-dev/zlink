using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace DeliveryDispatch.Server.Tracking;

[ZLinkHandlerGroup(SampleNames.TrackingRouteChannel)]
internal sealed class DeliveryStatusChangedHandler(
    EvidenceStore evidence,
    IZLinkActorManager actorDirectory,
    IZLinkActorClient actors,
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
        var actor = await actorDirectory.FindAsync(request.CustomerId, cancellationToken)
                    ?? throw new InvalidOperationException($"Customer actor '{request.CustomerId}' was not found.");
        await actors.SendToActor(SampleNames.MeshName, actor, updated)
            .Async(cancellationToken);
        logger.LogInformation(
            "deliverydispatch tracking: status delivery={DeliveryId} status={Status} courier={CourierId}",
            request.DeliveryId,
            request.Status,
            request.CourierId);
        return new DeliveryStatusChangedRes(request.DeliveryId, request.Status);
    }
}
