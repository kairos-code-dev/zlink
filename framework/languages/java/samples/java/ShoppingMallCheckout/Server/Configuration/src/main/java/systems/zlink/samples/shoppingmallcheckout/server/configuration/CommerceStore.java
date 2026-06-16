package systems.zlink.samples.shoppingmallcheckout.server.configuration;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Function;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.CartSeed;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.OrderLineInput;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.OrderState;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.PaymentMethodSeed;

/**
 * File-backed shared store for event stream, read-model projection, and
 * commerce business state.
 *
 * <p>Every mutation acquires an OS file lock, reads the whole JSON document,
 * applies the change, and writes it back. Both {@code CommerceApi} instances
 * and both {@code OrderWorkflow} instances share one document, which is the
 * sample's concurrency boundary.
 */
public final class CommerceStore {
    private final ObjectMapper json = new ObjectMapper();
    private final Path stateFile;
    private final Path lockFile;

    public CommerceStore() {
        String directory = System.getProperty(
            "zlink.samples.shoppingmallcheckout.storeDir",
            System.getenv().getOrDefault(
                "SHOPPINGMALL_STORE_DIR",
                Paths.get(System.getProperty("java.io.tmpdir"), "zlink-shoppingmall-store").toString()));
        Path dir = Paths.get(directory);
        try {
            Files.createDirectories(dir);
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to create store directory.", ex);
        }
        this.stateFile = dir.resolve("commerce-state.json");
        this.lockFile = dir.resolve("commerce-state.lock");
    }

    // ----- stored event record -----

    public record StoredEvent(
        String eventId,
        String sourceCommandId,
        String orderId,
        String eventType,
        JsonNode payload,
        long version,
        long createdAtUnixMs) {
    }

    public record ReserveInventoryResult(boolean accepted, String reservationId, String reason) {
    }

    public record AuthorizePaymentResult(boolean accepted, String paymentId, String reason) {
    }

    public record IdempotencyMapping(String idempotencyKey, String orderId, String ownerInstanceId, boolean started) {
    }

    public record StoreEvidence(
        Map<String, List<String>> eventsByOrder,
        int paymentFailureCount,
        int releasedReservationCount,
        int startedIdempotencyCount) {
    }

    // ----- seed -----

    public void seedDefaults() {
        mutate(state -> {
            ObjectNode carts = childObject(state, "carts");
            putCart(carts, new CartSeed("cart-success",
                List.of(new OrderLineInput("sku-keyboard", 1), new OrderLineInput("sku-mouse", 1)),
                120.00, "USD"));
            putCart(carts, new CartSeed("cart-inventory-fail",
                List.of(new OrderLineInput("sku-soldout", 2)), 88.00, "USD"));
            putCart(carts, new CartSeed("cart-payment-fail",
                List.of(new OrderLineInput("sku-headset", 1)), 64.00, "USD"));

            ObjectNode inventory = childObject(state, "inventory");
            putIfAbsentInt(inventory, "sku-keyboard", 5);
            putIfAbsentInt(inventory, "sku-mouse", 5);
            putIfAbsentInt(inventory, "sku-headset", 3);
            putIfAbsentInt(inventory, "sku-soldout", 1);

            ObjectNode payments = childObject(state, "paymentMethods");
            putPaymentMethod(payments, new PaymentMethodSeed("pm-ok", true, null));
            putPaymentMethod(payments, new PaymentMethodSeed("pm-decline", false, "payment declined"));

            ObjectNode addresses = childObject(state, "shippingAddresses");
            addresses.put("addr-home", true);
            addresses.put("addr-office", true);
            return null;
        });
    }

    // ----- commerce state (cart / address / payment validation) -----

    public Optional<CartSeed> findCart(String cartId) {
        return read(state -> {
            JsonNode carts = state.get("carts");
            if (carts == null || !carts.has(cartId)) {
                return Optional.empty();
            }
            return Optional.of(readCart(carts.get(cartId)));
        });
    }

    public boolean shippingAddressExists(String shippingAddressId) {
        return read(state -> {
            JsonNode addresses = state.get("shippingAddresses");
            return addresses != null && addresses.has(shippingAddressId);
        });
    }

    public boolean paymentMethodExists(String paymentMethodId) {
        return read(state -> {
            JsonNode methods = state.get("paymentMethods");
            return methods != null && methods.has(paymentMethodId);
        });
    }

    // ----- idempotency mapping -----

    public Optional<IdempotencyMapping> findIdempotency(String idempotencyKey) {
        return read(state -> findMapping(state, idempotencyKey));
    }

    public IdempotencyMapping reserveIdempotency(String idempotencyKey, String ownerInstanceId) {
        return mutate(state -> {
            Optional<IdempotencyMapping> existing = findMapping(state, idempotencyKey);
            if (existing.isPresent()) {
                return existing.get();
            }
            long next = state.path("nextOrderSequence").asLong(0) + 1;
            state.put("nextOrderSequence", next);
            String orderId = String.format("order-%04d", next);
            IdempotencyMapping mapping = new IdempotencyMapping(idempotencyKey, orderId, ownerInstanceId, false);
            writeMapping(state, mapping);
            return mapping;
        });
    }

    public void createPendingMapping(String idempotencyKey, String orderId, String ownerInstanceId) {
        mutate(state -> {
            writeMapping(state, new IdempotencyMapping(idempotencyKey, orderId, ownerInstanceId, false));
            return null;
        });
    }

    public void markIdempotencyStarted(String idempotencyKey) {
        mutate(state -> {
            findMapping(state, idempotencyKey).ifPresent(mapping ->
                writeMapping(state, new IdempotencyMapping(
                    mapping.idempotencyKey(), mapping.orderId(), mapping.ownerInstanceId(), true)));
            return null;
        });
    }

    public void saveOrderPaymentMethod(String orderId, String paymentMethodId) {
        mutate(state -> {
            childObject(state, "orderPaymentMethods").put(orderId, paymentMethodId);
            return null;
        });
    }

    public String orderPaymentMethod(String orderId) {
        return read(state -> {
            JsonNode methods = state.get("orderPaymentMethods");
            return methods == null ? null : methods.path(orderId).asText(null);
        });
    }

    // ----- inventory / payment modules -----

    public ReserveInventoryResult reserveInventory(String orderId, List<OrderLineInput> lines) {
        return mutate(state -> {
            ObjectNode inventory = childObject(state, "inventory");
            for (OrderLineInput line : lines) {
                int available = inventory.path(line.sku()).asInt(0);
                if (line.quantity() > available) {
                    return new ReserveInventoryResult(false, null, "inventory unavailable for " + line.sku());
                }
            }
            for (OrderLineInput line : lines) {
                int available = inventory.path(line.sku()).asInt(0);
                inventory.put(line.sku(), available - line.quantity());
            }
            String reservationId = "reservation-" + orderId;
            childObject(state, "reservations").put(reservationId, orderId);
            return new ReserveInventoryResult(true, reservationId, null);
        });
    }

    public void releaseInventory(String orderId, String reservationId, String reason) {
        mutate(state -> {
            childObject(state, "releasedReservations").put(reservationId, reason);
            return null;
        });
    }

    public AuthorizePaymentResult authorizePayment(
        String orderId, String paymentMethodId, double amount, String currency) {
        return mutate(state -> {
            JsonNode methods = state.get("paymentMethods");
            JsonNode method = methods == null ? null : methods.get(paymentMethodId);
            boolean shouldAuthorize = method != null && method.path("shouldAuthorize").asBoolean(false);
            if (!shouldAuthorize) {
                String reason = method == null
                    ? "payment method missing"
                    : method.path("failureReason").asText("payment failed");
                childObject(state, "paymentAttempts").put(orderId, reason);
                return new AuthorizePaymentResult(false, null, reason);
            }
            String paymentId = "payment-" + orderId;
            childObject(state, "payments").put(paymentId, String.format("%.2f %s", amount, currency));
            return new AuthorizePaymentResult(true, paymentId, null);
        });
    }

    // ----- event store -----

    public List<StoredEvent> readEvents(String orderId) {
        return read(state -> {
            List<StoredEvent> events = new ArrayList<>();
            JsonNode streams = state.get("events");
            if (streams == null || !streams.has(orderId)) {
                return events;
            }
            for (JsonNode node : streams.get(orderId)) {
                events.add(readStoredEvent(node));
            }
            events.sort((a, b) -> Long.compare(a.version(), b.version()));
            return events;
        });
    }

    /**
     * Appends events with optimistic version checking and semantic dedupe.
     *
     * @return the new stream version after the append.
     */
    public long appendEvents(String orderId, long expectedVersion, List<StoredEvent> events) {
        return mutate(state -> {
            ObjectNode streams = childObject(state, "events");
            com.fasterxml.jackson.databind.node.ArrayNode stream = streams.has(orderId)
                ? (com.fasterxml.jackson.databind.node.ArrayNode) streams.get(orderId)
                : streams.putArray(orderId);
            long currentVersion = 0;
            List<StoredEvent> existing = new ArrayList<>();
            for (JsonNode node : stream) {
                StoredEvent stored = readStoredEvent(node);
                existing.add(stored);
                currentVersion = Math.max(currentVersion, stored.version());
            }
            if (currentVersion != expectedVersion) {
                throw new IllegalStateException(
                    "Order stream version mismatch for '" + orderId + "': expected "
                        + expectedVersion + " but found " + currentVersion + ".");
            }
            for (StoredEvent event : events) {
                if (isDuplicate(existing, event)) {
                    continue;
                }
                long version = ++currentVersion;
                StoredEvent appended = new StoredEvent(
                    event.eventId(),
                    event.sourceCommandId(),
                    orderId,
                    event.eventType(),
                    event.payload(),
                    version,
                    event.createdAtUnixMs());
                stream.add(writeStoredEvent(appended));
                existing.add(appended);
            }
            return currentVersion;
        });
    }

    private boolean isDuplicate(List<StoredEvent> existing, StoredEvent candidate) {
        switch (candidate.eventType()) {
            case "OrderStartedEvent" -> {
                for (StoredEvent stored : existing) {
                    if ("OrderStartedEvent".equals(stored.eventType())
                        && candidate.sourceCommandId() != null
                        && candidate.sourceCommandId().equals(stored.sourceCommandId())) {
                        return true;
                    }
                }
            }
            case "InventoryReservedEvent" -> {
                String reservationId = candidate.payload().path("reservationId").asText(null);
                for (StoredEvent stored : existing) {
                    if ("InventoryReservedEvent".equals(stored.eventType())
                        && reservationId != null
                        && reservationId.equals(stored.payload().path("reservationId").asText(null))) {
                        return true;
                    }
                }
            }
            case "PaymentAuthorizedEvent" -> {
                String paymentId = candidate.payload().path("paymentId").asText(null);
                for (StoredEvent stored : existing) {
                    if ("PaymentAuthorizedEvent".equals(stored.eventType())
                        && paymentId != null
                        && paymentId.equals(stored.payload().path("paymentId").asText(null))) {
                        return true;
                    }
                }
            }
            case "OrderConfirmedEvent", "OrderFailedEvent" -> {
                for (StoredEvent stored : existing) {
                    if (candidate.eventType().equals(stored.eventType())) {
                        return true;
                    }
                }
            }
            default -> {
                return false;
            }
        }
        return false;
    }

    // ----- read model -----

    public Optional<OrderState> findReadModel(String orderId) {
        return read(state -> {
            JsonNode models = state.get("readModels");
            if (models == null || !models.has(orderId)) {
                return Optional.empty();
            }
            return Optional.of(readState(models.get(orderId)));
        });
    }

    public void saveReadModel(OrderState orderState) {
        mutate(state -> {
            childObject(state, "readModels").set(orderState.orderId(), writeState(orderState));
            return null;
        });
    }

    public boolean deleteReadModel(String orderId) {
        return mutate(state -> {
            ObjectNode models = childObject(state, "readModels");
            if (!models.has(orderId)) {
                return false;
            }
            models.remove(orderId);
            return true;
        });
    }

    // ----- evidence -----

    public StoreEvidence evidence(List<String> orderIds) {
        return read(state -> {
            Map<String, List<String>> eventsByOrder = new LinkedHashMap<>();
            JsonNode streams = state.get("events");
            for (String orderId : orderIds) {
                List<String> types = new ArrayList<>();
                if (streams != null && streams.has(orderId)) {
                    List<StoredEvent> sorted = new ArrayList<>();
                    for (JsonNode node : streams.get(orderId)) {
                        sorted.add(readStoredEvent(node));
                    }
                    sorted.sort((a, b) -> Long.compare(a.version(), b.version()));
                    for (StoredEvent stored : sorted) {
                        types.add(stored.eventType());
                    }
                }
                eventsByOrder.put(orderId, types);
            }
            int paymentFailureCount = sizeOf(state.get("paymentAttempts"));
            int releasedReservationCount = sizeOf(state.get("releasedReservations"));
            int startedCount = 0;
            JsonNode mappings = state.get("idempotency");
            if (mappings != null) {
                for (JsonNode mapping : mappings) {
                    if (mapping.path("started").asBoolean(false)) {
                        startedCount++;
                    }
                }
            }
            return new StoreEvidence(
                eventsByOrder, paymentFailureCount, releasedReservationCount, startedCount);
        });
    }

    // ----- json plumbing -----

    private Optional<IdempotencyMapping> findMapping(JsonNode state, String idempotencyKey) {
        JsonNode mappings = state.get("idempotency");
        if (mappings == null || !mappings.has(idempotencyKey)) {
            return Optional.empty();
        }
        JsonNode node = mappings.get(idempotencyKey);
        return Optional.of(new IdempotencyMapping(
            idempotencyKey,
            node.path("orderId").asText(),
            node.path("ownerInstanceId").asText(),
            node.path("started").asBoolean(false)));
    }

    private void writeMapping(ObjectNode state, IdempotencyMapping mapping) {
        ObjectNode mappings = childObject(state, "idempotency");
        ObjectNode node = json.createObjectNode();
        node.put("orderId", mapping.orderId());
        node.put("ownerInstanceId", mapping.ownerInstanceId());
        node.put("started", mapping.started());
        mappings.set(mapping.idempotencyKey(), node);
    }

    private CartSeed readCart(JsonNode node) {
        List<OrderLineInput> lines = new ArrayList<>();
        for (JsonNode line : node.path("lines")) {
            lines.add(new OrderLineInput(line.path("sku").asText(), line.path("quantity").asInt()));
        }
        return new CartSeed(
            node.path("cartId").asText(),
            lines,
            node.path("amount").asDouble(),
            node.path("currency").asText());
    }

    private void putCart(ObjectNode carts, CartSeed cart) {
        if (carts.has(cart.cartId())) {
            return;
        }
        ObjectNode node = json.createObjectNode();
        node.put("cartId", cart.cartId());
        com.fasterxml.jackson.databind.node.ArrayNode lines = node.putArray("lines");
        for (OrderLineInput line : cart.lines()) {
            ObjectNode lineNode = json.createObjectNode();
            lineNode.put("sku", line.sku());
            lineNode.put("quantity", line.quantity());
            lines.add(lineNode);
        }
        node.put("amount", cart.amount());
        node.put("currency", cart.currency());
        carts.set(cart.cartId(), node);
    }

    private void putPaymentMethod(ObjectNode methods, PaymentMethodSeed seed) {
        if (methods.has(seed.paymentMethodId())) {
            return;
        }
        ObjectNode node = json.createObjectNode();
        node.put("paymentMethodId", seed.paymentMethodId());
        node.put("shouldAuthorize", seed.shouldAuthorize());
        if (seed.failureReason() != null) {
            node.put("failureReason", seed.failureReason());
        }
        methods.set(seed.paymentMethodId(), node);
    }

    private void putIfAbsentInt(ObjectNode node, String key, int value) {
        if (!node.has(key)) {
            node.put(key, value);
        }
    }

    private StoredEvent readStoredEvent(JsonNode node) {
        return new StoredEvent(
            node.path("eventId").asText(),
            node.path("sourceCommandId").isNull() ? null : node.path("sourceCommandId").asText(null),
            node.path("orderId").asText(),
            node.path("eventType").asText(),
            node.path("payload"),
            node.path("version").asLong(),
            node.path("createdAtUnixMs").asLong());
    }

    private ObjectNode writeStoredEvent(StoredEvent event) {
        ObjectNode node = json.createObjectNode();
        node.put("eventId", event.eventId());
        if (event.sourceCommandId() == null) {
            node.putNull("sourceCommandId");
        } else {
            node.put("sourceCommandId", event.sourceCommandId());
        }
        node.put("orderId", event.orderId());
        node.put("eventType", event.eventType());
        node.set("payload", event.payload());
        node.put("version", event.version());
        node.put("createdAtUnixMs", event.createdAtUnixMs());
        return node;
    }

    private OrderState readState(JsonNode node) {
        return new OrderState(
            node.path("orderId").asText(),
            node.path("status").asText(),
            textOrNull(node, "shippingAddressId"),
            textOrNull(node, "reservationId"),
            textOrNull(node, "paymentId"),
            textOrNull(node, "reason"),
            node.has("amount") && !node.path("amount").isNull() ? node.path("amount").asDouble() : null,
            textOrNull(node, "currency"),
            node.path("updatedAtUnixMs").asLong());
    }

    private ObjectNode writeState(OrderState state) {
        ObjectNode node = json.createObjectNode();
        node.put("orderId", state.orderId());
        node.put("status", state.status());
        putNullable(node, "shippingAddressId", state.shippingAddressId());
        putNullable(node, "reservationId", state.reservationId());
        putNullable(node, "paymentId", state.paymentId());
        putNullable(node, "reason", state.reason());
        if (state.amount() == null) {
            node.putNull("amount");
        } else {
            node.put("amount", state.amount());
        }
        putNullable(node, "currency", state.currency());
        node.put("updatedAtUnixMs", state.updatedAtUnixMs());
        return node;
    }

    private static String textOrNull(JsonNode node, String field) {
        JsonNode value = node.get(field);
        return value == null || value.isNull() ? null : value.asText();
    }

    private void putNullable(ObjectNode node, String field, String value) {
        if (value == null) {
            node.putNull(field);
        } else {
            node.put(field, value);
        }
    }

    private static int sizeOf(JsonNode node) {
        return node == null ? 0 : node.size();
    }

    private ObjectNode childObject(ObjectNode state, String field) {
        JsonNode child = state.get(field);
        if (child instanceof ObjectNode object) {
            return object;
        }
        return state.putObject(field);
    }

    public ObjectMapper json() {
        return json;
    }

    // ----- locked read / mutate -----

    private <T> T read(Function<ObjectNode, T> reader) {
        return withState(reader, false);
    }

    private <T> T mutate(Function<ObjectNode, T> mutator) {
        return withState(mutator, true);
    }

    private <T> T withState(Function<ObjectNode, T> action, boolean write) {
        try (FileChannel channel = FileChannel.open(
            lockFile, StandardOpenOption.CREATE, StandardOpenOption.WRITE)) {
            FileLock lock = acquire(channel);
            try {
                ObjectNode state = loadState();
                T result = action.apply(state);
                if (write) {
                    saveState(state);
                }
                return result;
            } finally {
                lock.release();
            }
        } catch (IOException ex) {
            throw new UncheckedIOException("Failed to access commerce store.", ex);
        }
    }

    private FileLock acquire(FileChannel channel) throws IOException {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(30);
        FileLock lock = channel.tryLock();
        while (lock == null && System.nanoTime() < deadline) {
            LockSupport.parkNanos(java.util.concurrent.TimeUnit.MILLISECONDS.toNanos(10));
            lock = channel.tryLock();
        }
        if (lock == null) {
            throw new IllegalStateException("Timed out acquiring commerce store lock.");
        }
        return lock;
    }

    private ObjectNode loadState() throws IOException {
        if (!Files.exists(stateFile)) {
            return json.createObjectNode();
        }
        byte[] bytes = Files.readAllBytes(stateFile);
        if (bytes.length == 0) {
            return json.createObjectNode();
        }
        JsonNode node = json.readTree(bytes);
        return node instanceof ObjectNode object ? object : json.createObjectNode();
    }

    private void saveState(ObjectNode state) throws IOException {
        Files.write(
            stateFile,
            json.writerWithDefaultPrettyPrinter().writeValueAsString(state).getBytes(StandardCharsets.UTF_8),
            StandardOpenOption.CREATE,
            StandardOpenOption.TRUNCATE_EXISTING,
            StandardOpenOption.WRITE);
    }

    public List<OrderLineInput> linesFromCart(CartSeed cart) {
        return new ArrayList<>(cart.lines());
    }

    public Messages.OrderState placeholder(String orderId) {
        return SampleTopology.failedPlaceholder(orderId);
    }
}
