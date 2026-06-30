using ShoppingMall.Server.CommerceApi.Ports.Outbound;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.CommerceApi.Application.OrderWorkflow;

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
                                ?? throw new InvalidOperationException(
                                    $"Started order '{existing.OrderId}' has no projection.");
            return new StartOrderRes(existingState.OrderId, existingState.Status);
        }

        if (existing is not null
            && !string.Equals(existing.OwnerInstanceId, options.InstanceId, StringComparison.Ordinal))
            return await peers.ForwardStartAsync(existing.OwnerInstanceId, request, cancellationToken);

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