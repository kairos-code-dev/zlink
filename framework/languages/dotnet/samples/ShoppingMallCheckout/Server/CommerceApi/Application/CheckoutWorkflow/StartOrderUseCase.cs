using ShoppingMallCheckout.Server.CommerceApi.Ports.Outbound;
using ShoppingMallCheckout.Server.Shared.Ports.Outbound;
using ShoppingMallCheckout.Server.Configuration;
using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Server.CommerceApi.Application.CheckoutWorkflow;

internal sealed class StartOrderUseCase(
    ICommerceStateStore commerce,
    IOrderReadModelStore readModels,
    IOrderWorkflowRouter workflows,
    ICommerceApiPeerClient peers,
    CommerceApiInstanceOptions options)
{
    public async ValueTask<StartOrderRes> ExecuteAsync(
        StartOrderReq request,
        CancellationToken cancellationToken)
    {
        var existing = await commerce.FindIdempotencyAsync(request.IdempotencyKey, cancellationToken);
        if (existing is { Started: true })
        {
            var existingState = await readModels.FindAsync(existing.OrderId, cancellationToken)
                                ?? throw new InvalidOperationException($"Started order '{existing.OrderId}' has no projection.");
            return new StartOrderRes(existingState.OrderId, existingState.Status);
        }

        if (existing is not null
            && !string.Equals(existing.OwnerInstanceId, options.InstanceId, StringComparison.Ordinal))
        {
            return await peers.ForwardStartAsync(existing.OwnerInstanceId, request, cancellationToken);
        }

        var cart = await commerce.GetCartAsync(request.CartId, cancellationToken);
        await commerce.ValidateShippingAddressAsync(request.ShippingAddressId, cancellationToken);
        _ = await commerce.GetPaymentMethodAsync(request.PaymentMethodId, cancellationToken);

        var mapping = existing ?? await commerce.ReserveIdempotencyAsync(
            request.IdempotencyKey,
            options.InstanceId,
            cancellationToken);
        await commerce.SaveOrderPaymentMethodAsync(
            mapping.OrderId,
            request.PaymentMethodId,
            cancellationToken);

        var command = new StartOrderWorkflowReq(
            mapping.OrderId,
            request.CartId,
            request.ShippingAddressId,
            request.PaymentMethodId,
            request.IdempotencyKey,
            cart.Lines,
            cart.Amount,
            cart.Currency);
        var state = existing is null
            ? await workflows.StartAsync(command, cancellationToken)
            : await readModels.FindAsync(mapping.OrderId, cancellationToken)
              ?? await workflows.StartAsync(command, cancellationToken);
        return new StartOrderRes(state.OrderId, state.Status);
    }

    private async ValueTask<OrderState> WaitForProjectionAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.WorkflowTimeout);
        while (!timeout.IsCancellationRequested)
        {
            var projection = await readModels.FindAsync(orderId, timeout.Token);
            if (projection is not null)
            {
                return projection;
            }

            await Task.Delay(50, timeout.Token);
        }

        throw new TimeoutException($"Timed out waiting for order projection '{orderId}'.");
    }

}

internal sealed class GetOrderStateUseCase(
    IOrderReadModelStore readModels)
{
    public async ValueTask<GetOrderStateRes> ExecuteAsync(
        GetOrderStateReq request,
        CancellationToken cancellationToken)
    {
        var state = await readModels.FindAsync(request.OrderId, cancellationToken)
                    ?? throw new InvalidOperationException($"Order projection '{request.OrderId}' does not exist.");
        return new GetOrderStateRes(state);
    }
}
