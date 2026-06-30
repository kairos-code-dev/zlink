using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace DeliveryDispatch.Server.CourierActorNode.Spots.EntrySpot.Handlers;

internal sealed class BindCourierSessionActorHandler(ILogger<BindCourierSessionActorHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<CourierEntrySpot, CourierActor, BindCourierSessionReq, BindCourierSessionRes>
{
    public ValueTask<BindCourierSessionRes> HandleAsync(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorRequestContext context,
        BindCourierSessionReq message,
        CancellationToken cancellationToken)
    {
        var actorRef = message.Actor
            ?? throw new InvalidOperationException("Courier bind relay requires actor ref.");
        var sessionRoute = message.SessionRoute
            ?? throw new InvalidOperationException("Courier bind relay requires session route.");
        logger.LogInformation(
            "deliverydispatch courier-actor: session bound courier={CourierId} actor={ActorId}",
            message.CourierId,
            actor.ActorId);
        return ValueTask.FromResult(new BindCourierSessionRes(message.CourierId, actorRef, sessionRoute));
    }
}
