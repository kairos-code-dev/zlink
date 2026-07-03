using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class CourierSessionBinder(
    SampleTopology topology,
    IZLinkRouteClient routes,
    ILogger<CourierSessionBinder> logger)
{
    public async ValueTask<BindCourierSessionRes> BindAsync(
        string courierId,
        IZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        var actor = await FindOrEnsureActorAsync(courierId, cancellationToken);
        var boundActor = context.Actors.Find(actor.ActorId);
        if (boundActor is null)
        {
            boundActor = await context.Actors.BindAsync(
                new ActorRef(
                    RoutingId.From(actor.NodeRid),
                    actor.ActorId,
                    actor.Generation),
                cancellationToken);
        }

        logger.LogInformation(
            "deliverydispatch courier-session: bound courier={CourierId} node={NodeRid} session={SessionId}",
            courierId,
            actor.NodeRid,
            context.SessionId);

        await boundActor.RelayAsync(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(
                new BindCourierSessionReq(courierId, actor, context.SessionId)),
            cancellationToken);
        return new BindCourierSessionRes(courierId, actor, context.SessionId);
    }

    private async ValueTask<ActorRefSnapshot> FindOrEnsureActorAsync(
        string courierId,
        CancellationToken cancellationToken)
    {
        var placement = topology.CourierPlacement(courierId);
        var found = await routes.Request(
                SampleNames.CourierActorNodeRouteChannel,
                placement.NodeRid,
                new FindCourierActorReq(courierId))
            .PacketName(nameof(FindCourierActorReq))
            .Async<FindCourierActorRes>(cancellationToken);
        if (found.Actor is { } existing)
        {
            return existing;
        }

        var ensured = await routes.Request(
                SampleNames.CourierActorNodeRouteChannel,
                placement.NodeRid,
                new EnsureCourierActorReq(courierId))
            .PacketName(nameof(EnsureCourierActorReq))
            .Async<EnsureCourierActorRes>(cancellationToken);
        return ensured.Actor;
    }
}
