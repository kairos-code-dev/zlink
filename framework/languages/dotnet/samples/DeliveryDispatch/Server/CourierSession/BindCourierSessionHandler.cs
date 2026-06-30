using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class BindCourierSessionHandler(
    IZLinkChannelClient channels,
    ILogger<BindCourierSessionHandler> logger)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => nameof(BindCourierSession);

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<BindCourierSession>();
        var sessionRoute = context.RoutingId?.ToString()
            ?? throw new InvalidOperationException("Courier stream session has no routing id.");
        var bound = await channels.RequestToChannel(
                SampleNames.CourierRouteChannel,
                new BindCourier(request.CourierId, sessionRoute))
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

        logger.LogInformation(
            "deliverydispatch courier-session: bound courier={CourierId} session={SessionId}",
            bound.CourierId,
            context.SessionId);

        await boundActor.RelayAsync(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(
                new BindCourierSession(bound.CourierId, bound.Actor, bound.SessionRoute)),
            cancellationToken);
    }
}
