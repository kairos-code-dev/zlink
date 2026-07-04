using DeliveryDispatch.Server.Configuration;
using DeliveryDispatch.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Actors;

namespace DeliveryDispatch.Server.CustomerGateway;

internal sealed class CustomerActorAccess(
    IZLinkActorManager actorManager,
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

        var actorRef = actor.Value;
        return new FindCustomerActorRes(
            request.CustomerId,
            ZLinkActorRefSnapshot.From(actorRef));
    }

    public async ValueTask<EnsureCustomerActorRes> EnsureAsync(
        EnsureCustomerActorReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actorManager.GetOrCreateAsync(
            request.CustomerId,
            SampleNames.CustomerActorType,
            request,
            cancellationToken);
        logger.LogInformation(
            "deliverydispatch customer-access: ensured customer={CustomerId} node={NodeRid}",
            request.CustomerId,
            actor.NodeRid);
        return new EnsureCustomerActorRes(
            request.CustomerId,
            ZLinkActorRefSnapshot.From(actor));
    }
}
