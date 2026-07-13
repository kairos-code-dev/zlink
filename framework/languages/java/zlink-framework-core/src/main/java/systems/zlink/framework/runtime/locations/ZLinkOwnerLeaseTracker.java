package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.time.Instant;
import java.util.Comparator;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.LongSupplier;
import java.util.stream.Collectors;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;

final class ZLinkOwnerLeaseTracker {
    private final ZLinkOwnerLeaseStore store;
    private final Duration pollingInterval;
    private final LongSupplier nanoTime;
    private final Object refreshGate = new Object();
    private final Object liveSetGate = new Object();
    private volatile Snapshot snapshot;
    private String liveSetFingerprint;
    private long liveSetVersion;

    ZLinkOwnerLeaseTracker(ZLinkOwnerLeaseStore store, Duration pollingInterval) {
        this(store, pollingInterval, System::nanoTime);
    }

    ZLinkOwnerLeaseTracker(
        ZLinkOwnerLeaseStore store,
        Duration pollingInterval,
        LongSupplier nanoTime) {
        this.store = Objects.requireNonNull(store, "store");
        this.pollingInterval = requirePositive(pollingInterval, "pollingInterval");
        this.nanoTime = Objects.requireNonNull(nanoTime, "nanoTime");
    }

    CompletionStage<Boolean> isOwnerLive(String ownerId) {
        return getSnapshot().thenCompose(current -> {
            ZLinkOwnerLease lease = current.leases().get(ownerId);
            if (lease != null) {
                return CompletableFuture.completedFuture(
                    remaining(current, lease).compareTo(Duration.ZERO) > 0);
            }

            // A runtime can create its lease after this snapshot. Do not turn the
            // polling interval into a reconnect delay for rows owned by that runtime.
            return refreshSnapshot().thenApply(refreshed -> {
                ZLinkOwnerLease refreshedLease = refreshed.leases().get(ownerId);
                return refreshedLease != null
                    && remaining(refreshed, refreshedLease).compareTo(Duration.ZERO) > 0;
            });
        });
    }

    CompletionStage<Long> getLiveOwnerSetVersion() {
        return getSnapshot().thenApply(current -> {
            String fingerprint = current.leases().values().stream()
                .filter(lease -> remaining(current, lease).compareTo(Duration.ZERO) > 0)
                .map(ZLinkOwnerLease::ownerId)
                .sorted(Comparator.naturalOrder())
                .collect(Collectors.joining("\n"));
            synchronized (liveSetGate) {
                if (!Objects.equals(fingerprint, liveSetFingerprint)) {
                    liveSetFingerprint = fingerprint;
                    liveSetVersion++;
                }
                return liveSetVersion;
            }
        });
    }

    private CompletionStage<Snapshot> getSnapshot() {
        Snapshot current = snapshot;
        long now = nanoTime.getAsLong();
        if (current != null && Duration.ofNanos(now - current.fetchedAtNanos()).compareTo(pollingInterval) < 0) {
            return CompletableFuture.completedFuture(current);
        }

        synchronized (refreshGate) {
            current = snapshot;
            now = nanoTime.getAsLong();
            if (current != null && Duration.ofNanos(now - current.fetchedAtNanos()).compareTo(pollingInterval) < 0) {
                return CompletableFuture.completedFuture(current);
            }
            return refreshSnapshot();
        }
    }

    private CompletionStage<Snapshot> refreshSnapshot() {
        long fetchedAt = nanoTime.getAsLong();
        return store.listOwnerLeases().thenApply(listed -> {
            Map<String, ZLinkOwnerLease> leases = listed.leases().stream()
                .collect(Collectors.toUnmodifiableMap(
                    ZLinkOwnerLease::ownerId,
                    lease -> lease,
                    (left, right) -> right));
            Snapshot refreshed = new Snapshot(leases, listed.storeNow(), fetchedAt);
            synchronized (refreshGate) {
                Snapshot current = snapshot;
                if (current == null || refreshed.fetchedAtNanos() >= current.fetchedAtNanos()) {
                    snapshot = refreshed;
                    return refreshed;
                }
                return current;
            }
        });
    }

    private Duration remaining(Snapshot current, ZLinkOwnerLease lease) {
        Duration storeRemaining = Duration.between(current.storeNow(), lease.leaseExpiresAt());
        Duration elapsedSinceFetch = Duration.ofNanos(nanoTime.getAsLong() - current.fetchedAtNanos());
        return storeRemaining.minus(elapsedSinceFetch);
    }

    private static Duration requirePositive(Duration value, String name) {
        if (value == null || value.isZero() || value.isNegative()) {
            throw new IllegalArgumentException(name + " must be positive.");
        }
        return value;
    }

    private record Snapshot(
        Map<String, ZLinkOwnerLease> leases,
        Instant storeNow,
        long fetchedAtNanos) {
    }
}
