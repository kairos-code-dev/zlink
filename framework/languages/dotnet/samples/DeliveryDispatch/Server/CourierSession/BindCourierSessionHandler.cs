using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class BindCourierSessionHandler(IZLinkChannelClient channels)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(BindCouriers);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<BindCouriers>();
        var boundCouriers = new List<CourierBound>();
        var sessionRoute = context.RoutingId?.ToString()
            ?? throw new InvalidOperationException("Courier stream session has no routing id.");
        foreach (var courierId in request.CourierIds)
        {
            var bound = await channels.RequestToChannel(
                    SampleNames.CourierRouteChannel,
                    new BindCourier(courierId, sessionRoute))
                .Async<CourierBound>(cancellationToken);

            var boundActor = context.Actors.Find(bound.Actor.ActorId);
            if (boundActor is null)
            {
                boundActor = await context.Actors.BindAsync(
                    new ActorRef(
                        RoutingId.From(bound.Actor.NodeRid),
                        bound.Actor.ActorId,
                        bound.Actor.Generation),
                    cancellationToken);
            }

            await boundActor.RelayAsync(
                Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(
                    new CourierSessionAttached(bound.CourierId)),
                cancellationToken);

            boundCouriers.Add(bound);
            Console.Error.WriteLine(
                $"deliverydispatch courier-session: bound courier={bound.CourierId} session={context.SessionId}");
        }

        await context.Client.Reply(new CouriersBound(boundCouriers.ToArray())).Async();
    }
}
