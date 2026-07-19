using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;

namespace ShoppingMall.Server.CommerceApi.Infrastructure.ZLink;

internal sealed class ZLinkOrderWorkflowRouter(
    IZLinkRouteClient channels,
    SampleTopology topology) : IOrderWorkflowRouter
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await RequestToOwner(command.OrderId, command)
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await RequestToOwner(command.OrderId, command)
            .Async<ContinueOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken)
    {
        var response = await RequestToOwner(command.OrderId, command)
            .Async<RebuildOrderProjectionRes>(cancellationToken);
        return response.State;
    }

    public async ValueTask<OrderState> PrepareInventoryReservedCheckpointAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var response = await RequestToOwner(
                command.OrderId,
                new PrepareInventoryReservedCheckpointReq(command))
            .Async<StartOrderWorkflowRes>(cancellationToken);
        return response.State;
    }

    private IZLinkRequestCall RequestToOwner<TMessage>(string orderId, TMessage command)
    {
        var owner = topology.ForOrderId(orderId);
        return channels.RequestToChannel(SampleNames.OrderWorkflowChannelFor(owner.InstanceId), SampleNames.OrderWorkflowChannelFor(owner.InstanceId),
            command);
    }
}
