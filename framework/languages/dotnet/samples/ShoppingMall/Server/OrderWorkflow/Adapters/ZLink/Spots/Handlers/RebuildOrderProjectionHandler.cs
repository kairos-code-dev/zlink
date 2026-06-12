using ShoppingMall.Server.OrderWorkflow.Adapters.ZLink.Spots;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMall.Server.OrderWorkflow.Adapters.ZLink.Spots.Handlers;

internal sealed class RebuildOrderProjectionHandler :
    IZLinkSpotRequestHandler<OrderWorkflowSpot, RebuildOrderProjectionReq, RebuildOrderProjectionRes>
{
    public ValueTask<RebuildOrderProjectionRes> HandleAsync(
        OrderWorkflowSpot spot,
        RebuildOrderProjectionReq request,
        CancellationToken cancellationToken)
    {
        return spot.RebuildOrderProjectionAsync(request, cancellationToken);
    }
}
