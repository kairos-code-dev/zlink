using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Streams;

namespace DeliveryDispatch.Server.CourierSession;

internal sealed class CourierSessionBinder(
    IZLinkActorManager actors,
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
        var result = await actors
            .GetOrCreate(courierId, SampleNames.CourierActorType)
            .InMesh(SampleNames.MeshName)
            .Request(new EnsureCourierActorReq(courierId))
            .Async(cancellationToken);
        var actor = result switch
        {
            ZLinkActorCreateResult.Existing existing => existing.Actor,
            ZLinkActorCreateResult.Created created => created.Actor,
            ZLinkActorCreateResult.Rejected => throw new InvalidOperationException(
                $"Courier actor '{courierId}' creation was rejected."),
            _ => throw new InvalidOperationException(
                $"Courier actor '{courierId}' returned an unknown creation result.")
        };
        return ActorRefSnapshot.From(actor);
    }
}
