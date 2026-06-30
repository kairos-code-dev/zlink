using System.Runtime.CompilerServices;
using ShoppingMall.Client.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.HttpClient;

namespace ShoppingMall.Client;

internal sealed class ShoppingMallClientScenario
{
    public async ValueTask RunAsync(
        ZLinkHttpClient apiA,
        ZLinkHttpClient apiB,
        CancellationToken cancellationToken = default)
    {
        var successReq = new StartOrderReq(
            "cart-success",
            "addr-home",
            "pm-ok",
            "order-success-001");
        var success = apiA.Post("/orders/start").Body(successReq).Fetch<StartOrderRes>();
        Ensure(success.Status == OrderStatuses.Created);
        Ensure(!string.IsNullOrWhiteSpace(success.OrderId));

        var created = await GetOrderAsync(apiA, success.OrderId, cancellationToken);
        Ensure(IsCreatedOrConfirmed(created));
        Ensure(created.ShippingAddressId == successReq.ShippingAddressId);

        var confirmed = await WaitForStatusAsync(apiA, success.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(confirmed.ReservationId is not null);
        Ensure(confirmed.PaymentId is not null);
        Ensure(confirmed.Amount == 120.00m);
        Ensure(confirmed.Currency == "USD");

        var duplicate = apiB.Post("/orders/start").Body(successReq).Fetch<StartOrderRes>();
        Ensure(duplicate.OrderId == success.OrderId);

        var pendingReq = new StartOrderReq(
            "cart-success",
            "addr-office",
            "pm-ok",
            "order-pending-001");
        var pendingHook = await apiA.Post("/self-check/idempotency/pending")
            .Body(new
            {
                pendingReq.IdempotencyKey,
                OrderId = "order-pending-0001",
                OwnerInstanceId = "api-a"
            })
            .SubmitRawAsync(cancellationToken);
        Ensure(pendingHook.Status is >= 200 and < 300);
        var pending = apiB.Post("/orders/start").Body(pendingReq).Fetch<StartOrderRes>();
        Ensure(pending.OrderId == "order-pending-0001");
        Ensure(pending.Status == OrderStatuses.Created);
        var pendingCreated = await GetOrderAsync(apiA, pending.OrderId, cancellationToken);
        Ensure(IsCreatedOrConfirmed(pendingCreated));
        Ensure(pendingCreated.ShippingAddressId == pendingReq.ShippingAddressId);

        var inventoryReq = new StartOrderReq(
            "cart-inventory-fail",
            "addr-home",
            "pm-ok",
            "order-inventory-001");
        var inventoryStarted = apiA.Post("/orders/start").Body(inventoryReq).Fetch<StartOrderRes>();
        var inventoryFailed =
            await WaitForStatusAsync(apiA, inventoryStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(inventoryFailed.Reason?.Contains("inventory", StringComparison.OrdinalIgnoreCase) == true);

        var paymentReq = new StartOrderReq(
            "cart-payment-fail",
            "addr-home",
            "pm-decline",
            "order-payment-001");
        var paymentStarted = apiB.Post("/orders/start").Body(paymentReq).Fetch<StartOrderRes>();
        var paymentFailed =
            await WaitForStatusAsync(apiB, paymentStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(paymentFailed.ReservationId is not null);
        Ensure(paymentFailed.Reason?.Contains("payment", StringComparison.OrdinalIgnoreCase) == true);

        var deleteProjection = await apiA.Post($"/self-check/projection/{success.OrderId}/delete")
            .SubmitRawAsync(cancellationToken);
        Ensure(deleteProjection.Status is >= 200 and < 300);
        var rebuilt = apiA.Post($"/self-check/projection/{success.OrderId}/rebuild")
            .Fetch<RebuildOrderProjectionRes>();
        Ensure(rebuilt.State.Status == OrderStatuses.Confirmed);
        var rebuiltRead = await GetOrderAsync(apiB, success.OrderId, cancellationToken);
        Ensure(rebuiltRead.Status == OrderStatuses.Confirmed);

        var delayedFirst = await GetOrderAsync(apiB, paymentStarted.OrderId, cancellationToken);
        var delayedSecond = await GetOrderAsync(apiA, paymentStarted.OrderId, cancellationToken);
        Ensure(delayedFirst.Status == delayedSecond.Status);
        Ensure(delayedSecond.Status == OrderStatuses.Failed);

        var scaleReq = new StartOrderReq(
            "cart-success",
            "addr-office",
            "pm-ok",
            "order-scale-001");
        var scale = apiB.Post("/orders/start").Body(scaleReq).Fetch<StartOrderRes>();
        var scaleConfirmed = await WaitForStatusAsync(apiA, scale.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(scaleConfirmed.Status == OrderStatuses.Confirmed);

        var assertion = apiA.Post("/self-check/assert")
            .Body(new ServerAssertionReq(
                success.OrderId,
                pending.OrderId,
                inventoryStarted.OrderId,
                paymentStarted.OrderId,
                scale.OrderId))
            .Fetch<ServerAssertionRes>();
        Ensure(assertion.Passed);
    }

    private static ValueTask<OrderState> GetOrderAsync(
        ZLinkHttpClient api,
        string orderId,
        CancellationToken cancellationToken)
    {
        var response = api.Get($"/orders/{orderId}").Fetch<GetOrderStateRes>();
        return ValueTask.FromResult(response.State);
    }

    private static async ValueTask<OrderState> WaitForStatusAsync(
        ZLinkHttpClient api,
        string orderId,
        string expectedStatus,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.WorkflowTimeout);
        while (!timeout.IsCancellationRequested)
        {
            var state = await GetOrderAsync(api, orderId, timeout.Token);
            if (state.Status == expectedStatus) return state;

            await Task.Delay(100, timeout.Token);
        }

        throw new TimeoutException($"Timed out waiting for order '{orderId}' status '{expectedStatus}'.");
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))]
        string? expression = null)
    {
        if (!condition) throw new InvalidOperationException($"Ensure failed: {expression}");
    }

    private static bool IsCreatedOrConfirmed(OrderState state)
    {
        return state.Status == OrderStatuses.Created
               || state.Status == OrderStatuses.Confirmed;
    }
}