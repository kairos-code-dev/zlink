using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class CourierSessionBinder(
    SampleTopology topology,
    IZLinkSpotClient routes,
    IZLinkSpotHandleResolver spots,
    ILogger<CourierSessionBinder> logger)
{
    public async ValueTask<BindCourierSessionRes> BindAsync(
        string courierId,
        IZLinkSessionContext context,
        CancellationToken cancellationToken)
    {
        var actor = await FindOrEnsureActorAsync(courierId, cancellationToken);
        logger.LogInformation(
            "deliverydispatch courier-session: find/ensure returned courier={CourierId} node={NodeRid}",
            courierId,
            actor.NodeRid);
        var boundActor = await context.Actors.BindOrGetAsync(
            actor.ToActorRef(),
            cancellationToken);

        logger.LogInformation(
            "deliverydispatch courier-session: bound courier={CourierId} node={NodeRid} session={SessionId}",
            courierId,
            actor.NodeRid,
            context.SessionId);

        await boundActor.RelayAsync(
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(
                new BindCourierReq(courierId, context.SessionId)),
            cancellationToken);
        return new BindCourierSessionRes(courierId, actor, context.SessionId);
    }

    private async ValueTask<ActorRefSnapshot> FindOrEnsureActorAsync(
        string courierId,
        CancellationToken cancellationToken)
    {
        var placement = topology.CourierPlacement(courierId);
        var address = await spots.ResolveSpotHandleAsync(
                          SampleNames.MeshName,
                          placement.NodeRid,
                          cancellationToken)
                      ?? throw new InvalidOperationException(
                          $"Courier placement spot '{placement.NodeRid}' was not found.");
        var found = await routes.RequestToSpot(address,
                new FindCourierActorReq(courierId))
            .Async<FindCourierActorRes>(cancellationToken);
        if (found.Actor is { } existing)
        {
            return existing;
        }

        var ensured = await routes.RequestToSpot(address,
                new EnsureCourierActorReq(courierId))
            .Async<EnsureCourierActorRes>(cancellationToken);
        return ensured.Actor;
    }
}
