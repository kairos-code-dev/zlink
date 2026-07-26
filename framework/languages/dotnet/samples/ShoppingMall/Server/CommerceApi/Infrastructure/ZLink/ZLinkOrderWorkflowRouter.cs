using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.ZLink;

internal sealed class ZLinkOrderWorkflowRouter(
    IZLinkRouteClient channels) : IOrderWorkflowRouter
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await Request(command)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await Request(command)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken)
    {
        var response = await Request(command)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> PrepareInventoryReservedCheckpointAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await Request(
                new PrepareInventoryReservedCheckpointReq(command))
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    private IZLinkRequestCall Request<TMessage>(TMessage command) =>
        channels.RequestToChannel(
            SampleNames.OrderWorkflowChannel,
            command);
}
