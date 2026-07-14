using System.Runtime.CompilerServices;
using ShoppingMall.Client.Configuration;
using ShoppingMall.Shared.Contracts;
using Zlink.HttpClient;

namespace ShoppingMall.Client;

internal sealed class ShoppingMallClientScenario
{
    // End-to-end client story:
    // 1. Start a successful order on API A and wait until the workflow confirms it.
    // 2. Repeat the same idempotency key through API B and verify it returns the same order.
    // 3. Exercise a pending idempotency record that another API instance can resume.
    // 4. Run inventory and payment failure paths and verify the stored failure reasons.
    // 5. Delete and rebuild a read projection, then compare delayed reads across API instances.
    // 6. Start a scale-out order and ask the server to verify all expected workflow evidence.
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
        var success = apiA.Post("/orders/start").Body(successReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(success.Status == OrderStatuses.Created);
        Ensure(!string.IsNullOrWhiteSpace(success.OrderId));

        var created = await GetOrderAsync(apiA, success.OrderId, cancellationToken);
        Ensure(IsStartedOrConfirmed(created));
        Ensure(created.ShippingAddressId == successReq.ShippingAddressId);

        var confirmed = await WaitForStatusAsync(apiA, success.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(confirmed.ReservationId is not null);
        Ensure(confirmed.PaymentId is not null);
        Ensure(confirmed.Amount == 120.00m);
        Ensure(confirmed.Currency == "USD");

        var duplicate = apiB.Post("/orders/start").Body(successReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(duplicate.OrderId == success.OrderId);

        var concurrentReq = new StartOrderReq(
            "cart-success",
            "addr-office",
            "pm-ok",
            "order-concurrent-001");
        var concurrentA = Task.Run(() => apiA.Post("/orders/start").Body(concurrentReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body,
            cancellationToken);
        var concurrentB = Task.Run(() => apiB.Post("/orders/start").Body(concurrentReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body,
            cancellationToken);
        await Task.WhenAll(concurrentA, concurrentB);
        Ensure(concurrentA.Result.OrderId == concurrentB.Result.OrderId);
        var concurrentConfirmed =
            await WaitForStatusAsync(apiA, concurrentA.Result.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(concurrentConfirmed.Status == OrderStatuses.Confirmed);

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
            .AsyncRaw(cancellationToken);
        Ensure(pendingHook.Status is >= 200 and < 300);
        var pending = apiB.Post("/orders/start").Body(pendingReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(pending.OrderId == "order-pending-0001");
        Ensure(pending.Status == OrderStatuses.Created);
        var pendingCreated = await GetOrderAsync(apiA, pending.OrderId, cancellationToken);
        Ensure(IsStartedOrConfirmed(pendingCreated));
        Ensure(pendingCreated.ShippingAddressId == pendingReq.ShippingAddressId);

        var resumeReq = new StartOrderReq(
            "cart-success",
            "addr-home",
            "pm-ok",
            "order-resume-001");
        var inventoryReserved = apiA.Post("/self-check/workflow/inventory-reserved")
            .Body(resumeReq)
            .Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(inventoryReserved.Status == OrderStatuses.InventoryReserved);
        var resumed = apiB.Post($"/self-check/workflow/{inventoryReserved.OrderId}/continue")
            .Async<ContinueOrderWorkflowRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(resumed.State.Status == OrderStatuses.Confirmed);
        Ensure(resumed.State.ReservationId == $"reservation-{inventoryReserved.OrderId}");
        Ensure(resumed.State.PaymentId == $"payment-{inventoryReserved.OrderId}");

        var inventoryReq = new StartOrderReq(
            "cart-inventory-fail",
            "addr-home",
            "pm-ok",
            "order-inventory-001");
        var inventoryStarted = apiA.Post("/orders/start").Body(inventoryReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        var inventoryFailed =
            await WaitForStatusAsync(apiA, inventoryStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(inventoryFailed.Reason?.Contains("inventory", StringComparison.OrdinalIgnoreCase) == true);

        var paymentReq = new StartOrderReq(
            "cart-payment-fail",
            "addr-home",
            "pm-decline",
            "order-payment-001");
        var paymentStarted = apiB.Post("/orders/start").Body(paymentReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        var paymentFailed =
            await WaitForStatusAsync(apiB, paymentStarted.OrderId, OrderStatuses.Failed, cancellationToken);
        Ensure(paymentFailed.ReservationId is not null);
        Ensure(paymentFailed.Reason?.Contains("payment", StringComparison.OrdinalIgnoreCase) == true);

        var deleteProjection = await apiA.Post($"/self-check/projection/{success.OrderId}/delete")
            .AsyncRaw(cancellationToken);
        Ensure(deleteProjection.Status is >= 200 and < 300);
        var healedByContinue = apiB.Post($"/self-check/workflow/{success.OrderId}/continue")
            .Async<ContinueOrderWorkflowRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(healedByContinue.State.Status == OrderStatuses.Confirmed);

        var deleteProjectionAgain = await apiA.Post($"/self-check/projection/{success.OrderId}/delete")
            .AsyncRaw(cancellationToken);
        Ensure(deleteProjectionAgain.Status is >= 200 and < 300);
        var rebuilt = apiA.Post($"/self-check/projection/{success.OrderId}/rebuild")
            .Async<RebuildOrderProjectionRes>().AsTask().GetAwaiter().GetResult().Body;
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
        var scale = apiB.Post("/orders/start").Body(scaleReq).Async<StartOrderRes>().AsTask().GetAwaiter().GetResult().Body;
        var scaleConfirmed = await WaitForStatusAsync(apiA, scale.OrderId, OrderStatuses.Confirmed, cancellationToken);
        Ensure(scaleConfirmed.Status == OrderStatuses.Confirmed);

        var assertion = apiA.Post("/self-check/assert")
            .Body(new ServerAssertionReq(
                success.OrderId,
                pending.OrderId,
                concurrentA.Result.OrderId,
                inventoryReserved.OrderId,
                inventoryStarted.OrderId,
                paymentStarted.OrderId,
                scale.OrderId))
            .Async<ServerAssertionRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(assertion.Passed);
    }

    private static ValueTask<OrderState> GetOrderAsync(
        ZLinkHttpClient api,
        string orderId,
        CancellationToken cancellationToken)
    {
        var response = api.Get($"/orders/{orderId}").Async<GetOrderStateRes>().AsTask().GetAwaiter().GetResult().Body;
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

    private static bool IsStartedOrConfirmed(OrderState state)
    {
        return state.Status == OrderStatuses.Created
               || state.Status == OrderStatuses.InventoryReserved
               || state.Status == OrderStatuses.PaymentAuthorized
               || state.Status == OrderStatuses.Confirmed;
    }
}

internal sealed record ServerAssertionReq(
    string SuccessfulOrderId,
    string PendingRecoveredOrderId,
    string ConcurrentOrderId,
    string ResumedOrderId,
    string InventoryFailureOrderId,
    string PaymentFailureOrderId,
    string ScaleOutOrderId);

internal sealed record ServerAssertionRes(
    bool Passed,
    string[] Evidence);
