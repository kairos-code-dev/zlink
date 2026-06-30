using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class SubscribeDeliverySessionHandler(
    IZLinkChannelClient channels,
    ILogger<SubscribeDeliverySessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    private const string CustomerId = "customer-1";

    public string PacketName => nameof(SubscribeDelivery);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var ensured = await channels.RequestToChannel(
                SampleNames.CustomerRouteChannel,
                new EnsureCustomerActor(CustomerId))
            .Async<CustomerActorEnsured>(cancellationToken);

        var boundActor = context.Actors.Find(ensured.Actor.ActorId);
        if (boundActor is null)
        {
            boundActor = await context.Actors.BindAsync(
                new ActorRef(
                    RoutingId.From(ensured.Actor.NodeRid),
                    ensured.Actor.ActorId,
                    ensured.Actor.Generation),
                cancellationToken);
            logger.LogInformation(
                "deliverydispatch customer-session: bound customer actor={ActorId} session={SessionId}",
                ensured.Actor.ActorId,
                context.SessionId);
        }

        await boundActor.RelayAsync(payload, cancellationToken);
    }
}
