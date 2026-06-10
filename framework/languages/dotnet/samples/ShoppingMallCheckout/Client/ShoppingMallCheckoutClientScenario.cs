using System.Runtime.CompilerServices;
using System.Net.Http.Json;
using ShoppingMallCheckout.Shared.Configuration;
using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Client;

internal sealed class ShoppingMallCheckoutClientScenario
{
    public async ValueTask RunAsync(
        HttpClient apiA,
        HttpClient apiB,
        CancellationToken cancellationToken = default)
    {
        var successReq = new StartOrderReq(
            "cart-success",
            "addr-home",
            "pm-ok",
            "checkout-success-001");
        using var successResponse = await apiA.PostAsJsonAsync("/orders/start", successReq, cancellationToken);
        successResponse.EnsureSuccessStatusCode();
        var success = await successResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                      ?? throw new InvalidOperationException("StartOrderRes was empty.");
        Ensure(success.Status == OrderStatuses.Created);
        Ensure(!string.IsNullOrWhiteSpace(success.OrderId));

        var created = await GetOrderAsync(apiA, success.OrderId, cancellationToken);
        Ensure(created.Status == OrderStatuses.Created);
        Ensure(created.ShippingAddressId == successReq.ShippingAddressId);

        var confirmed = await WaitForStatusAsync(apiA, success.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(confirmed.ReservationId is not null);
        Ensure(confirmed.PaymentId is not null);
        Ensure(confirmed.Amount == 120.00m);
        Ensure(confirmed.Currency == "USD");

        using var duplicateResponse = await apiB.PostAsJsonAsync("/orders/start", successReq, cancellationToken);
        duplicateResponse.EnsureSuccessStatusCode();
        var duplicate = await duplicateResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                        ?? throw new InvalidOperationException("Duplicate StartOrderRes was empty.");
        Ensure(duplicate.OrderId == success.OrderId);

        var pendingReq = new StartOrderReq(
            "cart-success",
            "addr-office",
            "pm-ok",
            "checkout-pending-001");
        using var pendingHook = await apiA.PostAsJsonAsync(
            "/self-check/idempotency/pending",
            new
            {
                IdempotencyKey = pendingReq.IdempotencyKey,
                OrderId = "order-pending-0001",
                OwnerInstanceId = "api-a",
            },
            cancellationToken);
        pendingHook.EnsureSuccessStatusCode();
        using var pendingResponse = await apiB.PostAsJsonAsync("/orders/start", pendingReq, cancellationToken);
        pendingResponse.EnsureSuccessStatusCode();
        var pending = await pendingResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                      ?? throw new InvalidOperationException("Pending recovery StartOrderRes was empty.");
        Ensure(pending.OrderId == "order-pending-0001");
        Ensure(pending.Status == OrderStatuses.Created);
        var pendingCreated = await GetOrderAsync(apiA, pending.OrderId, cancellationToken);
        Ensure(pendingCreated.Status == OrderStatuses.Created);
        Ensure(pendingCreated.ShippingAddressId == pendingReq.ShippingAddressId);

        var inventoryReq = new StartOrderReq(
            "cart-inventory-fail",
            "addr-home",
            "pm-ok",
            "checkout-inventory-001");
        using var inventoryResponse = await apiA.PostAsJsonAsync("/orders/start", inventoryReq, cancellationToken);
        inventoryResponse.EnsureSuccessStatusCode();
        var inventoryStarted = await inventoryResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                               ?? throw new InvalidOperationException("Inventory failure StartOrderRes was empty.");
        var inventoryFailed = await WaitForStatusAsync(apiA, inventoryStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(inventoryFailed.Reason?.Contains("inventory", StringComparison.OrdinalIgnoreCase) == true);

        var paymentReq = new StartOrderReq(
            "cart-payment-fail",
            "addr-home",
            "pm-decline",
            "checkout-payment-001");
        using var paymentResponse = await apiB.PostAsJsonAsync("/orders/start", paymentReq, cancellationToken);
        paymentResponse.EnsureSuccessStatusCode();
        var paymentStarted = await paymentResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                             ?? throw new InvalidOperationException("Payment failure StartOrderRes was empty.");
        var paymentFailed = await WaitForStatusAsync(apiB, paymentStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(paymentFailed.ReservationId is not null);
        Ensure(paymentFailed.Reason?.Contains("payment", StringComparison.OrdinalIgnoreCase) == true);

        using var deleteProjection = await apiA.PostAsync($"/self-check/projection/{success.OrderId}/delete", content: null, cancellationToken);
        deleteProjection.EnsureSuccessStatusCode();
        using var rebuildProjection = await apiA.PostAsync($"/self-check/projection/{success.OrderId}/rebuild", content: null, cancellationToken);
        rebuildProjection.EnsureSuccessStatusCode();
        var rebuilt = await rebuildProjection.Content.ReadFromJsonAsync<RebuildOrderProjectionRes>(cancellationToken)
                      ?? throw new InvalidOperationException("Rebuild response was empty.");
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
            "checkout-scale-001");
        using var scaleResponse = await apiB.PostAsJsonAsync("/orders/start", scaleReq, cancellationToken);
        scaleResponse.EnsureSuccessStatusCode();
        var scale = await scaleResponse.Content.ReadFromJsonAsync<StartOrderRes>(cancellationToken)
                    ?? throw new InvalidOperationException("Scale-out StartOrderRes was empty.");
        var scaleConfirmed = await WaitForStatusAsync(apiA, scale.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(scaleConfirmed.Status == OrderStatuses.Confirmed);

        using var assertionResponse = await apiA.PostAsJsonAsync(
            "/self-check/assert",
            new ServerAssertionReq(
                success.OrderId,
                pending.OrderId,
                inventoryStarted.OrderId,
                paymentStarted.OrderId,
                scale.OrderId),
            cancellationToken);
        assertionResponse.EnsureSuccessStatusCode();
        var assertion = await assertionResponse.Content.ReadFromJsonAsync<ServerAssertionRes>(cancellationToken)
                        ?? throw new InvalidOperationException("Server assertion response was empty.");
        Ensure(assertion.Passed);
    }

    private static async ValueTask<OrderState> GetOrderAsync(
        HttpClient api,
        string orderId,
        CancellationToken cancellationToken)
    {
        using var response = await api.GetAsync($"/orders/{orderId}", cancellationToken);
        response.EnsureSuccessStatusCode();
        return (await response.Content.ReadFromJsonAsync<GetOrderStateRes>(cancellationToken)
               ?? throw new InvalidOperationException("GetOrderStateRes was empty.")).State;
    }

    private static async ValueTask<OrderState> WaitForStatusAsync(
        HttpClient api,
        string orderId,
        string expectedStatus,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.WorkflowTimeout);
        while (!timeout.IsCancellationRequested)
        {
            var state = await GetOrderAsync(api, orderId, timeout.Token);
            if (state.Status == expectedStatus)
            {
                return state;
            }

            await Task.Delay(100, timeout.Token);
        }

        throw new TimeoutException($"Timed out waiting for order '{orderId}' status '{expectedStatus}'.");
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }
}
