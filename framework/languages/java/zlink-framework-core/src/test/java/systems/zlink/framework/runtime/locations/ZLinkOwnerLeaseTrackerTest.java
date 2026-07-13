package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.concurrent.atomic.AtomicLong;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;

class ZLinkOwnerLeaseTrackerTest {
    private static final Instant NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from("node-a");

    @Test
    void ownerLivenessUsesStoreTimePlusMonotonicElapsedTime() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ManualTicker ticker = new ManualTicker();
        ZLinkOwnerLeaseTracker tracker = new ZLinkOwnerLeaseTracker(store, Duration.ofSeconds(30), ticker::nanos);
        store.renewOwnerLease("owner-a", NODE_A, Duration.ofSeconds(10)).toCompletableFuture().get();

        assertTrue(tracker.isOwnerLive("owner-a").toCompletableFuture().get());

        ticker.advance(Duration.ofSeconds(11));

        assertFalse(tracker.isOwnerLive("owner-a").toCompletableFuture().get());
    }

    @Test
    void liveOwnerSetVersionChangesOnlyWhenMembershipChanges() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ManualTicker ticker = new ManualTicker();
        ZLinkOwnerLeaseTracker tracker = new ZLinkOwnerLeaseTracker(store, Duration.ofMillis(1), ticker::nanos);

        long emptyVersion = tracker.getLiveOwnerSetVersion().toCompletableFuture().get();
        store.renewOwnerLease("owner-a", NODE_A, Duration.ofSeconds(30)).toCompletableFuture().get();
        ticker.advance(Duration.ofMillis(2));
        long ownerAddedVersion = tracker.getLiveOwnerSetVersion().toCompletableFuture().get();
        ticker.advance(Duration.ofMillis(2));
        store.renewOwnerLease("owner-a", NODE_A, Duration.ofSeconds(30)).toCompletableFuture().get();
        long renewedVersion = tracker.getLiveOwnerSetVersion().toCompletableFuture().get();
        store.removeOwnerLease("owner-a").toCompletableFuture().get();
        ticker.advance(Duration.ofMillis(2));
        long removedVersion = tracker.getLiveOwnerSetVersion().toCompletableFuture().get();

        assertEquals(1, emptyVersion);
        assertEquals(2, ownerAddedVersion);
        assertEquals(ownerAddedVersion, renewedVersion);
        assertEquals(3, removedVersion);
    }

    @Test
    void newlyStartedOwnerIsVisibleBeforePollingIntervalExpires() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ManualTicker ticker = new ManualTicker();
        ZLinkOwnerLeaseTracker tracker = new ZLinkOwnerLeaseTracker(
            store,
            Duration.ofSeconds(30),
            ticker::nanos);

        assertFalse(tracker.isOwnerLive("owner-new").toCompletableFuture().get());

        store.renewOwnerLease("owner-new", NODE_A, Duration.ofSeconds(30))
            .toCompletableFuture().get();

        assertTrue(tracker.isOwnerLive("owner-new").toCompletableFuture().get());
    }

    private static final class ManualTicker {
        private final AtomicLong nanos = new AtomicLong();

        long nanos() {
            return nanos.get();
        }

        void advance(Duration duration) {
            nanos.addAndGet(duration.toNanos());
        }
    }
}
