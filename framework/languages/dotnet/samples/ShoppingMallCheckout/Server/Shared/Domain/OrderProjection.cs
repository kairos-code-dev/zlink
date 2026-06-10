using ShoppingMallCheckout.Shared.Configuration;
using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Server.Shared.Domain;

public static class OrderProjection
{
    public static OrderState Apply(OrderState? current, OrderDomainEvent domainEvent)
    {
        return domainEvent switch
        {
            OrderStartedEvent started => new OrderState(
                started.OrderId,
                OrderStatuses.Created,
                started.ShippingAddressId,
                ReservationId: null,
                PaymentId: null,
                Reason: null,
                started.Amount,
                started.Currency,
                started.CreatedAtUnixMs),
            InventoryReservedEvent reserved => Ensure(current, reserved).With(
                Status: OrderStatuses.InventoryReserved,
                ReservationId: reserved.ReservationId,
                UpdatedAtUnixMs: reserved.CreatedAtUnixMs),
            InventoryReservationFailedEvent failed => Ensure(current, failed).With(
                Status: OrderStatuses.Failed,
                Reason: failed.Reason,
                UpdatedAtUnixMs: failed.CreatedAtUnixMs),
            PaymentAuthorizedEvent paid => Ensure(current, paid).With(
                Status: OrderStatuses.PaymentAuthorized,
                PaymentId: paid.PaymentId,
                UpdatedAtUnixMs: paid.CreatedAtUnixMs),
            PaymentFailedEvent failed => Ensure(current, failed).With(
                Status: OrderStatuses.Failed,
                Reason: failed.Reason,
                UpdatedAtUnixMs: failed.CreatedAtUnixMs),
            InventoryReleasedEvent released => Ensure(current, released).With(
                Reason: released.Reason,
                UpdatedAtUnixMs: released.CreatedAtUnixMs),
            OrderConfirmedEvent confirmed => Ensure(current, confirmed).With(
                Status: OrderStatuses.Confirmed,
                UpdatedAtUnixMs: confirmed.ConfirmedAtUnixMs),
            OrderFailedEvent failed => Ensure(current, failed).With(
                Status: OrderStatuses.Failed,
                Reason: failed.Reason,
                UpdatedAtUnixMs: failed.FailedAtUnixMs),
            _ => throw new InvalidOperationException($"Unsupported order event '{domainEvent.GetType()}'."),
        };
    }

    private static OrderState Ensure(OrderState? current, OrderDomainEvent domainEvent)
    {
        return current ?? throw new InvalidOperationException(
            $"Projection cannot apply '{domainEvent.GetType().Name}' before OrderStartedEvent.");
    }

    private static OrderState With(
        this OrderState current,
        string? Status = null,
        string? ReservationId = null,
        string? PaymentId = null,
        string? Reason = null,
        long? UpdatedAtUnixMs = null)
    {
        return current with
        {
            Status = Status ?? current.Status,
            ReservationId = ReservationId ?? current.ReservationId,
            PaymentId = PaymentId ?? current.PaymentId,
            Reason = Reason ?? current.Reason,
            UpdatedAtUnixMs = UpdatedAtUnixMs ?? current.UpdatedAtUnixMs,
        };
    }
}
