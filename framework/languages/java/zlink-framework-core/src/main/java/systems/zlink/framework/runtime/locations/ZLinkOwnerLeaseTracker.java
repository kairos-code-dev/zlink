package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.LongSupplier;
import systems.zlink.framework.locations.ZLinkOwnerLeaseFound;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;

final class ZLinkOwnerLeaseTracker {
    private final ZLinkOwnerLeaseStore store;
    private final Duration pollingInterval;
    private final LongSupplier nanoTime;
    private final Map<String, ObservedLease> observed =
        new ConcurrentHashMap<>();

    ZLinkOwnerLeaseTracker(
        ZLinkOwnerLeaseStore store,
        Duration pollingInterval) {
        this(store, pollingInterval, System::nanoTime);
    }

    ZLinkOwnerLeaseTracker(
        ZLinkOwnerLeaseStore store,
        Duration pollingInterval,
        LongSupplier nanoTime) {
        this.store = Objects.requireNonNull(store, "store");
        this.pollingInterval = requirePositive(
            pollingInterval,
            "pollingInterval");
        this.nanoTime = Objects.requireNonNull(nanoTime, "nanoTime");
    }

    CompletionStage<Boolean> isOwnerLive(String ownerId) {
        ObservedLease current = observed.get(ownerId);
        long now = nanoTime.getAsLong();
        if (current != null
            && Duration.ofNanos(now - current.fetchedAtNanos)
                .compareTo(pollingInterval) < 0) {
            return CompletableFuture.completedFuture(
                current.remaining(now).compareTo(Duration.ZERO) > 0);
        }
        return store.readOwnerLease(ownerId).thenApply(result -> {
            long fetchedAt = nanoTime.getAsLong();
            if (result instanceof ZLinkOwnerLeaseFound found) {
                ObservedLease refreshed = new ObservedLease(
                    Duration.between(
                        found.storeNow(),
                        found.leaseExpiresAt()),
                    fetchedAt);
                observed.put(ownerId, refreshed);
                return refreshed.remaining(fetchedAt)
                    .compareTo(Duration.ZERO) > 0;
            }
            observed.remove(ownerId);
            return false;
        });
    }

    private static Duration requirePositive(
        Duration value,
        String name) {
        if (value == null || value.isZero() || value.isNegative()) {
            throw new IllegalArgumentException(
                name + " must be positive.");
        }
        return value;
    }

    private record ObservedLease(
        Duration storeRemaining,
        long fetchedAtNanos) {
        private Duration remaining(long nowNanos) {
            return storeRemaining.minus(
                Duration.ofNanos(nowNanos - fetchedAtNanos));
        }
    }
}
