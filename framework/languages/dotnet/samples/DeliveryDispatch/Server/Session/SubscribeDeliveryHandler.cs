using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.Session;

internal sealed class SubscribeDeliveryHandler(
    IZLinkChannelClient channels,
    CustomerSessionDirectory sessions)
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
        var deliveryId = payload.Decode<SubscribeDelivery>().DeliveryId;
        var ensured = await channels.RequestToChannel(
                SampleNames.TrackingRouteChannel,
                new EnsureCustomerActor(CustomerId))
            .Async<CustomerActorEnsured>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(
                RoutingId.From(ensured.Actor.NodeRid),
                ensured.Actor.ActorId,
                ensured.Actor.Generation),
            cancellationToken);
        Console.Error.WriteLine($"deliverydispatch session: bound customer actor={ensured.Actor.ActorId}");

        var subscribed = await channels.RequestToChannel(
                SampleNames.TrackingRouteChannel,
                new SubscribeCustomerToDelivery(CustomerId, deliveryId))
            .Async<CustomerDeliverySubscribed>(cancellationToken);

        sessions.Subscribe(context, subscribed.DeliveryId);
        await context.Client.Reply(new SubscribeDeliveryAccepted(subscribed.DeliveryId))
            .Async();
    }
}
