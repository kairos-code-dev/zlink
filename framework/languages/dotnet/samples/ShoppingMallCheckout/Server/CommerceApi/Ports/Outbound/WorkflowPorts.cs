using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Server.CommerceApi.Ports.Outbound;

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

internal sealed record CommerceApiInstanceOptions(string InstanceId);
