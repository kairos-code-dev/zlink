using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot.Handlers;

internal sealed class CourierDecisionActorHandler(ILogger<CourierDecisionActorHandler> logger)
    : IZLinkEntrySpotActorSendHandler<CourierEntrySpot, CourierActor, CourierDecision>
{
    public ValueTask HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorSendContext context,
        CourierDecision message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Complete(message);
        logger.LogInformation(
            "deliverydispatch courier-actor: decision delivery={DeliveryId} courier={CourierId} accepted={Accepted}",
            message.DeliveryId,
            actor.ActorId,
            message.Accepted);
        return ValueTask.CompletedTask;
    }
}
