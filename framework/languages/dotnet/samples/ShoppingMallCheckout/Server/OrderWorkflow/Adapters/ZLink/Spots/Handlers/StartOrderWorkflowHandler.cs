using ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Spots;
using ShoppingMallCheckout.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace ShoppingMallCheckout.Server.OrderWorkflow.Adapters.ZLink.Spots.Handlers;

internal sealed class StartOrderWorkflowHandler :
    IZLinkSpotRequestHandler<OrderWorkflowSpot, StartOrderWorkflowReq, StartOrderWorkflowRes>
{
    public ValueTask<StartOrderWorkflowRes> HandleAsync(
        OrderWorkflowSpot spot,
        StartOrderWorkflowReq request,
        CancellationToken cancellationToken)
    {
        return spot.StartOrderWorkflowAsync(request, cancellationToken);
    }
}
