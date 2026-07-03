using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.CommerceApi.Ports.Outbound;

internal interface IOrderWorkflowRouter
{
    ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken);

    ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken);

    ValueTask<OrderState> RebuildProjectionAsync(
        RebuildOrderProjectionReq command,
        CancellationToken cancellationToken);
}

internal interface ICommerceApiPeerClient
{
    ValueTask<StartOrderRes> ForwardStartAsync(
        string ownerInstanceId,
        StartOrderReq request,
        CancellationToken cancellationToken);
}

internal interface IOrderWorkflowSelfCheckClient
{
    ValueTask<OrderState> PrepareInventoryReservedCheckpointAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken);
}

internal sealed record CommerceApiInstanceOptions(string InstanceId);
