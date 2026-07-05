using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.ZLink;

internal sealed class ZLinkOrderWorkflowRouter(
    IZLinkChannelClient channels) : IOrderWorkflowRouter
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await channels.RequestToChannel(SampleNames.OrderWorkflowRouteChannel, command)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await channels.RequestToChannel(SampleNames.OrderWorkflowRouteChannel, command)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken)
    {
        var response = await channels.RequestToChannel(SampleNames.OrderWorkflowRouteChannel, command)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        return response.State;
    }
}
