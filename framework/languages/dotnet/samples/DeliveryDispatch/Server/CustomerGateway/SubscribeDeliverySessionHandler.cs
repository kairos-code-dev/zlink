using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class SubscribeDeliverySessionHandler(
    CustomerActorAccess actors,
    CustomerActorDirectory directory,
    ILogger<SubscribeDeliverySessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, SubscribeDeliveryReq>
{
    private const string CustomerId = "customer-1";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        SubscribeDeliveryReq request,
        CancellationToken cancellationToken)
    {
        var found = await actors.FindAsync(
            new FindCustomerActorReq(CustomerId),
            cancellationToken);
        var actor = found.Actor;
        if (actor is null)
        {
            var ensured = await actors.EnsureAsync(
                new EnsureCustomerActorReq(CustomerId),
                cancellationToken);
            actor = ensured.Actor;
        }

        var boundActor = await context.Actors.BindOrGetAsync(
            actor.ToActorRef(),
            cancellationToken);
        logger.LogInformation(
            "deliverydispatch customer-session: bound customer actor={ActorId} session={SessionId}",
            actor.ActorId,
            context.SessionId);

        directory.Subscribe(CustomerId, request.DeliveryId);
        logger.LogInformation(
            "deliverydispatch customer-session: subscribed customer={CustomerId} delivery={DeliveryId}",
            CustomerId,
            request.DeliveryId);
        await context.Client.Reply(new SubscribeDeliveryRes(request.DeliveryId))
            .SubmitAsync(cancellationToken);
    }
}
