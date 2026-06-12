using ShoppingMall.Server.OrderWorkflow.Domain.ShoppingMall;
using ShoppingMall.Server.Shared.Domain;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Server.Configuration;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.OrderWorkflow.Application.OrderWorkflow;

internal sealed class OrderWorkflowService(
    IOrderEventStore events,
    IOrderReadModelStore readModels,
    ICommerceStateStore commerce)
{
    public async ValueTask<OrderState> StartAsync(
        StartOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var stored = await events.ReadAsync(command.OrderId, cancellationToken);
        var aggregate = OrderAggregate.Rehydrate(stored.Select(static item => item.Payload));
        if (aggregate.HasProcessedCommand(command.IdempotencyKey))
        {
            return await RequireProjectionAsync(command.OrderId, cancellationToken);
        }

        var now = NowUnixMs();
        var started = aggregate.Start(command, NewEventId("started", command.OrderId), now);
        if (started.Count > 0)
        {
            await AppendAndProjectAsync(command.OrderId, stored.Count, started, cancellationToken);
        }

        await commerce.MarkIdempotencyStartedAsync(command.IdempotencyKey, cancellationToken);
        var createdState = await RequireProjectionAsync(command.OrderId, cancellationToken);
        return createdState;
    }

    public async ValueTask<OrderState> ContinueAsync(
        ContinueOrderWorkflowReq command,
        CancellationToken cancellationToken)
    {
        var orderId = command.OrderId;
        var paymentMethodId = await commerce.GetOrderPaymentMethodAsync(orderId, cancellationToken);
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var stored = await events.ReadAsync(orderId, cancellationToken);
            var aggregate = OrderAggregate.Rehydrate(stored.Select(static item => item.Payload));
            if (aggregate.IsTerminal)
            {
                return await RequireProjectionAsync(orderId, cancellationToken);
            }

            var current = await RequireProjectionAsync(orderId, cancellationToken);
            IReadOnlyList<OrderDomainEvent> next = current.Status switch
            {
                OrderStatuses.Created => aggregate.ApplyInventoryResult(
                    await commerce.ReserveInventoryAsync(
                        orderId,
                        stored.OfTypeStored<OrderStartedEvent>().Single().Lines,
                        cancellationToken),
                    NewEventId("inventory", orderId),
                    NewEventId("failed", orderId),
                    NowUnixMs()),
                OrderStatuses.InventoryReserved => await AuthorizePaymentAsync(
                    aggregate,
                    current,
                    paymentMethodId,
                    cancellationToken),
                OrderStatuses.PaymentAuthorized => aggregate.Confirm(NewEventId("confirmed", orderId), NowUnixMs()),
                _ => [],
            };

            if (next.Count == 0)
            {
                return current;
            }

            await AppendAndProjectAsync(orderId, stored.Count, next, cancellationToken);
            foreach (var released in next.OfType<InventoryReleasedEvent>())
            {
                await commerce.ReleaseInventoryAsync(
                    orderId,
                    released.ReservationId,
                    released.Reason,
                    cancellationToken);
            }
        }
    }

    public async ValueTask<OrderState> RebuildProjectionAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        var stored = await events.ReadAsync(orderId, cancellationToken);
        OrderState? state = null;
        foreach (var storedEvent in stored)
        {
            state = OrderProjection.Apply(state, storedEvent.Payload);
        }

        if (state is null)
        {
            throw new InvalidOperationException($"Order '{orderId}' has no event stream.");
        }

        await readModels.SaveAsync(state, cancellationToken);
        return state;
    }

    private async ValueTask<IReadOnlyList<OrderDomainEvent>> AuthorizePaymentAsync(
        OrderAggregate aggregate,
        OrderState current,
        string paymentMethodId,
        CancellationToken cancellationToken)
    {
        var result = await commerce.AuthorizePaymentAsync(
            current.OrderId,
            paymentMethodId,
            current.Amount ?? throw new InvalidOperationException("Order amount is required."),
            current.Currency ?? throw new InvalidOperationException("Order currency is required."),
            cancellationToken);
        return aggregate.ApplyPaymentResult(
            result,
            NewEventId("payment", current.OrderId),
            NewEventId("release", current.OrderId),
            NewEventId("failed", current.OrderId),
            NowUnixMs());
    }

    private async ValueTask AppendAndProjectAsync(
        string orderId,
        long expectedVersion,
        IReadOnlyList<OrderDomainEvent> domainEvents,
        CancellationToken cancellationToken)
    {
        await events.AppendAsync(orderId, expectedVersion, domainEvents, cancellationToken);
        var current = await readModels.FindAsync(orderId, cancellationToken);
        foreach (var domainEvent in domainEvents)
        {
            current = OrderProjection.Apply(current, domainEvent);
            await readModels.SaveAsync(current, cancellationToken);
        }
    }

    private async ValueTask<OrderState> RequireProjectionAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        return await readModels.FindAsync(orderId, cancellationToken)
               ?? throw new InvalidOperationException($"Order projection '{orderId}' does not exist.");
    }

    private static string NewEventId(string prefix, string orderId)
    {
        return $"{prefix}-{orderId}-{Guid.NewGuid():N}";
    }

    private static long NowUnixMs()
    {
        return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }
}

internal static class StoredOrderEventExtensions
{
    public static IEnumerable<TEvent> OfTypeStored<TEvent>(this IEnumerable<StoredOrderEvent> events)
        where TEvent : OrderDomainEvent
    {
        return events.Select(static item => item.Payload).OfType<TEvent>();
    }
}
