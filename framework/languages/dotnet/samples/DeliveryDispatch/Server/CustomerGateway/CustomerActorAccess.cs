using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class CustomerActorAccess(
    IZLinkActorManager actorManager,
    CustomerActorDirectory directory,
    ILogger<CustomerActorAccess> logger)
{
    public async ValueTask<FindCustomerActorRes> FindAsync(
        FindCustomerActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actorManager.FindAsync(request.CustomerId, cancellationToken);
        if (actor is null)
        {
            return new FindCustomerActorRes(request.CustomerId, null);
        }

        var actorRef = directory.RequireRef(request.CustomerId);
        return new FindCustomerActorRes(
            request.CustomerId,
            ActorRefSnapshot.From(actorRef));
    }

    public async ValueTask<EnsureCustomerActorRes> EnsureAsync(
        EnsureCustomerActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = (await actorManager.GetOrCreate(request.CustomerId, SampleNames.CustomerActorType)
            .Request(request).Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Customer Actor creation was rejected.")
        };
        logger.LogInformation(
            "deliverydispatch customer-access: ensured customer={CustomerId} node={NodeRid}",
            request.CustomerId,
            actor.NodeRid);
        var actorRef = directory.RequireRef(request.CustomerId);
        return new EnsureCustomerActorRes(
            request.CustomerId,
            ActorRefSnapshot.From(actorRef));
    }
}
