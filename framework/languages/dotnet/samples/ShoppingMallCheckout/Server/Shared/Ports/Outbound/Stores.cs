using ShoppingMallCheckout.Server.Shared.Domain;
using ShoppingMallCheckout.Shared.Contracts;

namespace ShoppingMallCheckout.Server.Shared.Ports.Outbound;

public interface IOrderEventStore
{
    ValueTask<IReadOnlyList<StoredOrderEvent>> ReadAsync(
        string orderId,
        CancellationToken cancellationToken);

    ValueTask AppendAsync(
        string orderId,
        long expectedVersion,
        IReadOnlyList<OrderDomainEvent> events,
        CancellationToken cancellationToken);
}

public interface IOrderReadModelStore
{
    ValueTask<OrderState?> FindAsync(
        string orderId,
        CancellationToken cancellationToken);

    ValueTask SaveAsync(
        OrderState state,
        CancellationToken cancellationToken);

    ValueTask DeleteAsync(
        string orderId,
        CancellationToken cancellationToken);
}

public interface ICommerceStateStore
{
    ValueTask<CartSeed> GetCartAsync(
        string cartId,
        CancellationToken cancellationToken);

    ValueTask ValidateShippingAddressAsync(
        string shippingAddressId,
        CancellationToken cancellationToken);

    ValueTask<PaymentMethodSeed> GetPaymentMethodAsync(
        string paymentMethodId,
        CancellationToken cancellationToken);

    ValueTask<IdempotencyMapping?> FindIdempotencyAsync(
        string idempotencyKey,
        CancellationToken cancellationToken);

    ValueTask<IdempotencyMapping> ReserveIdempotencyAsync(
        string idempotencyKey,
        string ownerInstanceId,
        CancellationToken cancellationToken);

    ValueTask MarkIdempotencyStartedAsync(
        string idempotencyKey,
        CancellationToken cancellationToken);

    ValueTask CreatePendingMappingAsync(
        string idempotencyKey,
        string orderId,
        string ownerInstanceId,
        CancellationToken cancellationToken);

    ValueTask SaveOrderPaymentMethodAsync(
        string orderId,
        string paymentMethodId,
        CancellationToken cancellationToken);

    ValueTask<string> GetOrderPaymentMethodAsync(
        string orderId,
        CancellationToken cancellationToken);

    ValueTask<ReserveInventoryResult> ReserveInventoryAsync(
        string orderId,
        IReadOnlyList<OrderLineInput> lines,
        CancellationToken cancellationToken);

    ValueTask ReleaseInventoryAsync(
        string orderId,
        string reservationId,
        string reason,
        CancellationToken cancellationToken);

    ValueTask<AuthorizePaymentResult> AuthorizePaymentAsync(
        string orderId,
        string paymentMethodId,
        decimal amount,
        string currency,
        CancellationToken cancellationToken);
}

public sealed record StoredOrderEvent(
    string EventId,
    string? SourceCommandId,
    string OrderId,
    string EventType,
    OrderDomainEvent Payload,
    long Version,
    long CreatedAtUnixMs);

public sealed record IdempotencyMapping(
    string IdempotencyKey,
    string OrderId,
    string OwnerInstanceId,
    bool Started);

public sealed record StoreEvidence(
    IReadOnlyDictionary<string, string[]> EventsByOrder,
    int PaymentFailureCount,
    int ReleasedReservationCount,
    int StartedIdempotencyCount);
