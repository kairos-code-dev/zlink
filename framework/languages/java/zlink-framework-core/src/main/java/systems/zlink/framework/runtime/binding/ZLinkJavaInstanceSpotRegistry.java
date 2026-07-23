package systems.zlink.framework.runtime.binding;

import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;

/**
 * Framework-owned Instance Spot activation barrier. Only an Instance-marked
 * direct operation calls this registry; ordinary missing Spot routes do not.
 */
final class ZLinkJavaInstanceSpotRegistry {
    private final Map<String, BiFunction<RoutingId, Long, ZLinkBackendSpot>>
        factories =
        new ConcurrentHashMap<>();
    private final Map<RoutingId, String> stableTypes =
        new ConcurrentHashMap<>();
    private final Map<RoutingId, CompletableFuture<Activation>> activations =
        new ConcurrentHashMap<>();

    void register(
        String stableType,
        BiFunction<RoutingId, Long, ZLinkBackendSpot> factory) {
        requireType(stableType);
        if (factories.putIfAbsent(
            stableType,
            Objects.requireNonNull(factory, "factory")) != null) {
            throw new IllegalStateException(
                "Instance Spot type is already registered: " + stableType);
        }
    }

    CompletionStage<Activation> activate(
        RoutingId spotRid,
        String requestedType,
        long objectGeneration) {
        Objects.requireNonNull(spotRid, "spotRid");
        if (objectGeneration <= 0) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException(
                    "Instance Spot object generation must be positive"));
        }
        String selected = selectType(spotRid, requestedType);
        String recorded = stableTypes.putIfAbsent(spotRid, selected);
        if (recorded != null && !recorded.equals(selected)) {
            return CompletableFuture.failedFuture(
                new IllegalStateException(
                    "Instance Spot stable type does not match authority"));
        }
        CompletableFuture<Activation> current = activations.get(spotRid);
        if (current != null) {
            return current.thenCompose(activation ->
                activation.spot().lifecycleGeneration() == objectGeneration
                    ? CompletableFuture.completedFuture(activation)
                    : CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "Instance Spot object generation is stale")));
        }
        CompletableFuture<Activation> candidate = new CompletableFuture<>();
        current = activations.putIfAbsent(spotRid, candidate);
        if (current != null) {
            return current.thenCompose(activation ->
                activation.spot().lifecycleGeneration() == objectGeneration
                    ? CompletableFuture.completedFuture(activation)
                    : CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "Instance Spot object generation is stale")));
        }
        try {
            ZLinkBackendSpot spot =
                factories.get(selected).apply(spotRid, objectGeneration);
            if (spot == null) {
                throw new IllegalStateException(
                    "Instance Spot factory returned null");
            }
            if (spot.lifecycleGeneration() != objectGeneration) {
                throw new IllegalStateException(
                    "Instance Spot factory returned a stale generation");
            }
            candidate.complete(new Activation(selected, spot));
        } catch (Throwable failure) {
            candidate.completeExceptionally(failure);
            activations.remove(spotRid, candidate);
        }
        return candidate;
    }

    boolean close(RoutingId spotRid, long generation) {
        CompletableFuture<Activation> current = activations.get(spotRid);
        if (current == null || !current.isDone()) {
            return false;
        }
        Activation activation;
        try {
            activation = current.join();
        } catch (RuntimeException failure) {
            return false;
        }
        if (activation.spot().lifecycleGeneration() != generation
            || !activations.remove(spotRid, current)) {
            return false;
        }
        activation.spot().close();
        return true;
    }

    void closeAll() {
        activations.values().forEach(current -> {
            if (!current.isDone() || current.isCompletedExceptionally()) {
                return;
            }
            current.join().spot().close();
        });
        activations.clear();
        stableTypes.clear();
        factories.clear();
    }

    private String selectType(
        RoutingId spotRid,
        String requestedType) {
        String stored = stableTypes.get(spotRid);
        if (stored != null) {
            if (requestedType != null && !stored.equals(requestedType)) {
                throw new IllegalStateException(
                    "Instance Spot stable type does not match authority");
            }
            return stored;
        }
        if (requestedType != null) {
            requireType(requestedType);
            if (!factories.containsKey(requestedType)) {
                throw new IllegalStateException(
                    "Instance Spot type is not registered: " + requestedType);
            }
            return requestedType;
        }
        if (factories.size() != 1) {
            throw new IllegalStateException(
                "Instance Spot type is required unless exactly one type is registered");
        }
        return factories.keySet().iterator().next();
    }

    private static void requireType(String value) {
        if (value == null
            || value.isBlank()
            || value.indexOf('\0') >= 0
            || value.getBytes(java.nio.charset.StandardCharsets.UTF_8).length
                > 0xff) {
            throw new IllegalArgumentException(
                "Instance Spot stable type must be text8");
        }
    }

    record Activation(String stableType, ZLinkBackendSpot spot) {
    }
}
