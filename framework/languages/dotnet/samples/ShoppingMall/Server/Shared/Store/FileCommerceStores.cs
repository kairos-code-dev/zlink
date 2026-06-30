using System.Text.Json;
using ShoppingMall.Server.Shared.Domain;
using ShoppingMall.Server.Shared.Ports.Outbound;
using ShoppingMall.Shared.Contracts;

namespace ShoppingMall.Server.Shared.Store;

public sealed class FileCommerceStores(string directory) :
    IOrderEventStore,
    IOrderReadModelStore,
    ICommerceStateStore
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true
    };

    private readonly string _lockFile = Path.Combine(directory, "commerce-state.lock");

    private readonly string _stateFile = Path.Combine(directory, "commerce-state.json");

    public ValueTask<CartSeed> GetCartAsync(
        string cartId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(WithState(state => state.Carts.TryGetValue(cartId, out var cart)
            ? cart
            : throw new InvalidOperationException($"Cart '{cartId}' does not exist.")));
    }

    public ValueTask ValidateShippingAddressAsync(
        string shippingAddressId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state =>
        {
            if (!state.ShippingAddresses.Contains(shippingAddressId))
                throw new InvalidOperationException($"Shipping address '{shippingAddressId}' does not exist.");
        });
        return ValueTask.CompletedTask;
    }

    public ValueTask<PaymentMethodSeed> GetPaymentMethodAsync(
        string paymentMethodId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(WithState(state => state.PaymentMethods.TryGetValue(paymentMethodId, out var method)
            ? method
            : throw new InvalidOperationException($"Payment method '{paymentMethodId}' does not exist.")));
    }

    public ValueTask<IdempotencyMapping?> FindIdempotencyAsync(
        string idempotencyKey,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var mapping = WithState(state => state.Idempotency.GetValueOrDefault(idempotencyKey));
        return ValueTask.FromResult(mapping);
    }

    public ValueTask<IdempotencyMapping> ReserveIdempotencyAsync(
        string idempotencyKey,
        string ownerInstanceId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var mapping = WithState(state =>
        {
            if (state.Idempotency.TryGetValue(idempotencyKey, out var existing)) return existing;

            var orderId = $"order-{++state.NextOrderSequence:0000}";
            var created = new IdempotencyMapping(idempotencyKey, orderId, ownerInstanceId, false);
            state.Idempotency[idempotencyKey] = created;
            return created;
        });
        return ValueTask.FromResult(mapping);
    }

    public ValueTask MarkIdempotencyStartedAsync(
        string idempotencyKey,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state =>
        {
            var mapping = state.Idempotency[idempotencyKey];
            state.Idempotency[idempotencyKey] = mapping with { Started = true };
        });
        return ValueTask.CompletedTask;
    }

    public ValueTask CreatePendingMappingAsync(
        string idempotencyKey,
        string orderId,
        string ownerInstanceId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state =>
        {
            state.Idempotency[idempotencyKey] = new IdempotencyMapping(
                idempotencyKey,
                orderId,
                ownerInstanceId,
                false);
        });
        return ValueTask.CompletedTask;
    }

    public ValueTask<ReserveInventoryResult> ReserveInventoryAsync(
        string orderId,
        IReadOnlyList<OrderLineInput> lines,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var result = WithState(state =>
        {
            foreach (var line in lines)
            {
                var available = state.Inventory.GetValueOrDefault(line.Sku);
                if (available < line.Quantity)
                    return new ReserveInventoryResult(false, null, $"inventory unavailable for {line.Sku}");
            }

            foreach (var line in lines) state.Inventory[line.Sku] -= line.Quantity;

            var reservationId = $"reservation-{orderId}";
            state.Reservations[reservationId] = orderId;
            return new ReserveInventoryResult(true, reservationId, null);
        });
        return ValueTask.FromResult(result);
    }

    public ValueTask SaveOrderPaymentMethodAsync(
        string orderId,
        string paymentMethodId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state => state.OrderPaymentMethods[orderId] = paymentMethodId);
        return ValueTask.CompletedTask;
    }

    public ValueTask<string> GetOrderPaymentMethodAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var paymentMethodId = WithState(state => state.OrderPaymentMethods.TryGetValue(orderId, out var current)
            ? current
            : throw new InvalidOperationException($"Payment method for order '{orderId}' does not exist."));
        return ValueTask.FromResult(paymentMethodId);
    }

    public ValueTask ReleaseInventoryAsync(
        string orderId,
        string reservationId,
        string reason,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state => state.ReleasedReservations[reservationId] = reason);
        return ValueTask.CompletedTask;
    }

    public ValueTask<AuthorizePaymentResult> AuthorizePaymentAsync(
        string orderId,
        string paymentMethodId,
        decimal amount,
        string currency,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var result = WithState(state =>
        {
            var method = state.PaymentMethods[paymentMethodId];
            if (!method.ShouldAuthorize)
            {
                state.PaymentAttempts[orderId] = method.FailureReason ?? "payment failed";
                return new AuthorizePaymentResult(false, null, state.PaymentAttempts[orderId]);
            }

            var paymentId = $"payment-{orderId}";
            state.Payments[paymentId] = $"{amount:0.00} {currency}";
            return new AuthorizePaymentResult(true, paymentId, null);
        });
        return ValueTask.FromResult(result);
    }

    public ValueTask<IReadOnlyList<StoredOrderEvent>> ReadAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var events = WithState(state => state.Events.TryGetValue(orderId, out var current)
            ? current.OrderBy(static item => item.Version).ToArray()
            : []);
        return ValueTask.FromResult<IReadOnlyList<StoredOrderEvent>>(events);
    }

    public ValueTask AppendAsync(
        string orderId,
        long expectedVersion,
        IReadOnlyList<OrderDomainEvent> events,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state =>
        {
            if (!state.Events.TryGetValue(orderId, out var stream))
            {
                stream = [];
                state.Events[orderId] = stream;
            }

            var currentVersion = stream.Count == 0 ? 0 : stream.Max(static item => item.Version);
            if (currentVersion != expectedVersion)
                throw new InvalidOperationException(
                    $"Order stream version mismatch. order={orderId}, expected={expectedVersion}, actual={currentVersion}");

            foreach (var domainEvent in events)
            {
                var sourceCommandId = domainEvent is OrderStartedEvent started ? started.SourceCommandId : null;
                if (sourceCommandId is not null
                    && stream.Any(item => string.Equals(item.SourceCommandId, sourceCommandId, StringComparison.Ordinal)
                                          && item.EventType == nameof(OrderStartedEvent)))
                    continue;

                if (IsDuplicateSemanticEvent(stream, domainEvent)) continue;

                stream.Add(new StoredOrderEvent(
                    domainEvent.EventId,
                    sourceCommandId,
                    orderId,
                    domainEvent.GetType().Name,
                    domainEvent,
                    ++currentVersion,
                    domainEvent.CreatedAtUnixMs));
            }
        });
        return ValueTask.CompletedTask;
    }

    public ValueTask<OrderState?> FindAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var state = WithState(current => current.ReadModels.GetValueOrDefault(orderId));
        return ValueTask.FromResult(state);
    }

    public ValueTask SaveAsync(
        OrderState state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(current => current.ReadModels[state.OrderId] = state);
        return ValueTask.CompletedTask;
    }

    public ValueTask DeleteAsync(
        string orderId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        WithState(state => state.ReadModels.Remove(orderId));
        return ValueTask.CompletedTask;
    }

    public void SeedDefaults()
    {
        Directory.CreateDirectory(directory);
        WithState(state =>
        {
            state.Carts.TryAdd("cart-success", new CartSeed(
                "cart-success",
                [new OrderLineInput("sku-keyboard", 1), new OrderLineInput("sku-mouse", 1)],
                120.00m,
                "USD"));
            state.Carts.TryAdd("cart-inventory-fail", new CartSeed(
                "cart-inventory-fail",
                [new OrderLineInput("sku-soldout", 2)],
                88.00m,
                "USD"));
            state.Carts.TryAdd("cart-payment-fail", new CartSeed(
                "cart-payment-fail",
                [new OrderLineInput("sku-headset", 1)],
                64.00m,
                "USD"));

            state.Inventory.TryAdd("sku-keyboard", 5);
            state.Inventory.TryAdd("sku-mouse", 5);
            state.Inventory.TryAdd("sku-headset", 3);
            state.Inventory.TryAdd("sku-soldout", 1);

            state.PaymentMethods.TryAdd("pm-ok", new PaymentMethodSeed("pm-ok", true, null));
            state.PaymentMethods.TryAdd("pm-decline", new PaymentMethodSeed("pm-decline", false, "payment declined"));

            state.ShippingAddresses.Add("addr-home");
            state.ShippingAddresses.Add("addr-office");
        });
    }

    public ValueTask<StoreEvidence> EvidenceAsync(
        IReadOnlyList<string> orderIds,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var evidence = WithState(state =>
        {
            var events = orderIds.ToDictionary(
                static orderId => orderId,
                orderId => state.Events.TryGetValue(orderId, out var stream)
                    ? stream.Select(static item => item.EventType).ToArray()
                    : [],
                StringComparer.Ordinal);
            return new StoreEvidence(
                events,
                state.PaymentAttempts.Count,
                state.ReleasedReservations.Count,
                state.Idempotency.Values.Count(static item => item.Started));
        });
        return ValueTask.FromResult(evidence);
    }

    private static bool IsDuplicateSemanticEvent(
        IReadOnlyList<StoredOrderEvent> stream,
        OrderDomainEvent domainEvent)
    {
        return domainEvent switch
        {
            InventoryReservedEvent reserved => stream.Any(item =>
                item.Payload is InventoryReservedEvent current
                && current.ReservationId == reserved.ReservationId),
            PaymentAuthorizedEvent paid => stream.Any(item =>
                item.Payload is PaymentAuthorizedEvent current
                && current.PaymentId == paid.PaymentId),
            OrderConfirmedEvent => stream.Any(static item => item.Payload is OrderConfirmedEvent),
            OrderFailedEvent => stream.Any(static item => item.Payload is OrderFailedEvent),
            _ => false
        };
    }

    private T WithState<T>(Func<PersistedCommerceState, T> action)
    {
        using var guard = AcquireLock();
        var state = ReadState();
        var result = action(state);
        WriteState(state);
        return result;
    }

    private void WithState(Action<PersistedCommerceState> action)
    {
        WithState(state =>
        {
            action(state);
            return true;
        });
    }

    private FileStream AcquireLock()
    {
        Directory.CreateDirectory(directory);
        while (true)
            try
            {
                return new FileStream(_lockFile, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
            }
            catch (IOException)
            {
                Thread.Sleep(10);
            }
    }

    private PersistedCommerceState ReadState()
    {
        if (!File.Exists(_stateFile)) return new PersistedCommerceState();

        var json = File.ReadAllText(_stateFile);
        return string.IsNullOrWhiteSpace(json)
            ? new PersistedCommerceState()
            : JsonSerializer.Deserialize<PersistedCommerceState>(json, JsonOptions) ?? new PersistedCommerceState();
    }

    private void WriteState(PersistedCommerceState state)
    {
        File.WriteAllText(_stateFile, JsonSerializer.Serialize(state, JsonOptions));
    }
}

internal sealed class PersistedCommerceState
{
    public long NextOrderSequence { get; set; }

    public Dictionary<string, CartSeed> Carts { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, int> Inventory { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, PaymentMethodSeed> PaymentMethods { get; set; } = new(StringComparer.Ordinal);

    public HashSet<string> ShippingAddresses { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, IdempotencyMapping> Idempotency { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, List<StoredOrderEvent>> Events { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, OrderState> ReadModels { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, string> Reservations { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, string> ReleasedReservations { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, string> PaymentAttempts { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, string> Payments { get; set; } = new(StringComparer.Ordinal);

    public Dictionary<string, string> OrderPaymentMethods { get; set; } = new(StringComparer.Ordinal);
}