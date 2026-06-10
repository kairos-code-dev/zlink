using System.Text.Json.Serialization;
using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Server.Shared.Domain;

[JsonDerivedType(typeof(OrderStartedEvent), nameof(OrderStartedEvent))]
[JsonDerivedType(typeof(InventoryReservedEvent), nameof(InventoryReservedEvent))]
[JsonDerivedType(typeof(InventoryReservationFailedEvent), nameof(InventoryReservationFailedEvent))]
[JsonDerivedType(typeof(PaymentAuthorizedEvent), nameof(PaymentAuthorizedEvent))]
[JsonDerivedType(typeof(PaymentFailedEvent), nameof(PaymentFailedEvent))]
[JsonDerivedType(typeof(InventoryReleasedEvent), nameof(InventoryReleasedEvent))]
[JsonDerivedType(typeof(OrderConfirmedEvent), nameof(OrderConfirmedEvent))]
[JsonDerivedType(typeof(OrderFailedEvent), nameof(OrderFailedEvent))]
public abstract record OrderDomainEvent(
    string EventId,
    string OrderId,
    long CreatedAtUnixMs);

public sealed record OrderStartedEvent(
    string EventId,
    string SourceCommandId,
    string OrderId,
    string CartId,
    string ShippingAddressId,
    OrderLineInput[] Lines,
    decimal Amount,
    string Currency,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record InventoryReservedEvent(
    string EventId,
    string OrderId,
    string ReservationId,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record InventoryReservationFailedEvent(
    string EventId,
    string OrderId,
    string Reason,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record PaymentAuthorizedEvent(
    string EventId,
    string OrderId,
    string PaymentId,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record PaymentFailedEvent(
    string EventId,
    string OrderId,
    string Reason,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record InventoryReleasedEvent(
    string EventId,
    string OrderId,
    string ReservationId,
    string Reason,
    long CreatedAtUnixMs) : OrderDomainEvent(EventId, OrderId, CreatedAtUnixMs);

public sealed record OrderConfirmedEvent(
    string EventId,
    string OrderId,
    long ConfirmedAtUnixMs) : OrderDomainEvent(EventId, OrderId, ConfirmedAtUnixMs);

public sealed record OrderFailedEvent(
    string EventId,
    string OrderId,
    string Reason,
    long FailedAtUnixMs) : OrderDomainEvent(EventId, OrderId, FailedAtUnixMs);

public sealed record ReserveInventoryResult(
    bool Accepted,
    string? ReservationId,
    string? Reason);

public sealed record AuthorizePaymentResult(
    bool Accepted,
    string? PaymentId,
    string? Reason);
