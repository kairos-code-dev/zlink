using ShoppingMall.Server.Configuration;
using ShoppingMall.Server.Shared.Domain;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.OrderWorkflow.Domain.ShoppingMall;

internal sealed class OrderAggregate
{
    private readonly HashSet<string> _sourceCommands = new(StringComparer.Ordinal);

    public string OrderId { get; private set; } = string.Empty;

    public string Status { get; private set; } = string.Empty;

    public string? ReservationId { get; private set; }

    public bool HasStarted => !string.IsNullOrWhiteSpace(OrderId);

    public bool IsTerminal => Status is OrderStatuses.Confirmed or OrderStatuses.Failed;

    public bool HasProcessedCommand(string sourceCommandId)
    {
        return _sourceCommands.Contains(sourceCommandId);
    }

    public IReadOnlyList<OrderDomainEvent> Start(StartOrderWorkflowReq command, string eventId, long nowUnixMs)
    {
        if (HasStarted) return [];

        return
        [
            new OrderStartedEvent(
                eventId,
                command.IdempotencyKey,
                command.OrderId,
                command.CartId,
                command.ShippingAddressId,
                command.Lines,
                command.Amount,
                command.Currency,
                nowUnixMs)
        ];
    }

    public IReadOnlyList<OrderDomainEvent> ApplyInventoryResult(
        ReserveInventoryResult result,
        string eventId,
        string failureEventId,
        long nowUnixMs)
    {
        if (IsTerminal || Status != OrderStatuses.Created) return [];

        if (!result.Accepted)
        {
            var reason = result.Reason ?? "inventory unavailable";
            return
            [
                new InventoryReservationFailedEvent(eventId, OrderId, reason, nowUnixMs),
                new OrderFailedEvent(failureEventId, OrderId, reason, nowUnixMs)
            ];
        }

        return
        [
            new InventoryReservedEvent(
                eventId,
                OrderId,
                result.ReservationId ?? throw new InvalidOperationException("Accepted reservation requires an id."),
                nowUnixMs)
        ];
    }

    public IReadOnlyList<OrderDomainEvent> ApplyPaymentResult(
        AuthorizePaymentResult result,
        string eventId,
        string releaseEventId,
        string failureEventId,
        long nowUnixMs)
    {
        if (IsTerminal || Status != OrderStatuses.InventoryReserved) return [];

        if (!result.Accepted)
        {
            var reason = result.Reason ?? "payment failed";
            return
            [
                new PaymentFailedEvent(eventId, OrderId, reason, nowUnixMs),
                new InventoryReleasedEvent(
                    releaseEventId,
                    OrderId,
                    ReservationId ?? throw new InvalidOperationException("Reservation is required for compensation."),
                    reason,
                    nowUnixMs),
                new OrderFailedEvent(failureEventId, OrderId, reason, nowUnixMs)
            ];
        }

        return
        [
            new PaymentAuthorizedEvent(
                eventId,
                OrderId,
                result.PaymentId ?? throw new InvalidOperationException("Accepted payment requires an id."),
                nowUnixMs)
        ];
    }

    public IReadOnlyList<OrderDomainEvent> Confirm(string eventId, long nowUnixMs)
    {
        if (IsTerminal || Status != OrderStatuses.PaymentAuthorized) return [];

        return [new OrderConfirmedEvent(eventId, OrderId, nowUnixMs)];
    }

    public void Apply(OrderDomainEvent domainEvent)
    {
        switch (domainEvent)
        {
            case OrderStartedEvent started:
                OrderId = started.OrderId;
                Status = OrderStatuses.Created;
                _sourceCommands.Add(started.SourceCommandId);
                break;
            case InventoryReservedEvent reserved:
                ReservationId = reserved.ReservationId;
                Status = OrderStatuses.InventoryReserved;
                break;
            case InventoryReservationFailedEvent:
                Status = OrderStatuses.Failed;
                break;
            case PaymentAuthorizedEvent:
                Status = OrderStatuses.PaymentAuthorized;
                break;
            case PaymentFailedEvent:
                Status = OrderStatuses.Failed;
                break;
            case InventoryReleasedEvent:
                break;
            case OrderConfirmedEvent:
                Status = OrderStatuses.Confirmed;
                break;
            case OrderFailedEvent:
                Status = OrderStatuses.Failed;
                break;
        }
    }

    public static OrderAggregate Rehydrate(IEnumerable<OrderDomainEvent> events)
    {
        var aggregate = new OrderAggregate();
        foreach (var domainEvent in events) aggregate.Apply(domainEvent);

        return aggregate;
    }
}