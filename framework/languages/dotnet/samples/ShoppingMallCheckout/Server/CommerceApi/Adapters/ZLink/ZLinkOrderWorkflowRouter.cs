using ShoppingMallCheckout.Server.CommerceApi.Ports.Outbound;
using ShoppingMallCheckout.Server.Configuration;
using ShoppingMallCheckout.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace ShoppingMallCheckout.Server.CommerceApi.Adapters.ZLink;

internal sealed class ZLinkOrderWorkflowRouter(
    IZLinkRouteClient routes,
    SampleTopology topology) : IOrderWorkflowRouter
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.Request(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.Request(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.Request(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        return response.State;
    }
}
