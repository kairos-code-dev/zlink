using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.ZLink;

internal sealed class ZLinkOrderWorkflowRouter(
    IZLinkRouteClient routes,
    SampleTopology topology) : IOrderWorkflowRouter
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.RequestToNode(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.RequestToNode(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken)
    {
        var owner = topology.ForOrderId(command.OrderId);
        var response = await routes.RequestToNode(SampleNames.OrderWorkflowRouteChannel, owner.RouteRid, command)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        return response.State;
    }
}
