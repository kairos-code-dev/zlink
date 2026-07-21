package systems.zlink.framework.runtime.messaging;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.time.Duration;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CancellationException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;

final class ZLinkAdmissionRuntimeTest {
    @Test
    void duplicateGuardIsOwnedByOneCallAndReturnsAnExceptionalStage() {
        ZLinkOneWayCallGate first = new ZLinkOneWayCallGate();
        ZLinkOneWayCallGate second = new ZLinkOneWayCallGate();

        assertNull(first.begin());
        assertNull(second.begin());
        assertThrows(
            CompletionException.class,
            () -> first.begin()
                .toCompletableFuture()
                .join());
    }

    @Test
    void firstAttemptIsImmediateAndOnlyTheExactReadyDestinationRetries() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        AtomicInteger attempts = new AtomicInteger();
        AtomicInteger cleanups = new AtomicInteger();
        ZLinkBackendAdmissionKey target = ZLinkBackendAdmissionKey.channel("orders");

        var result = ZLinkSubmitResults.submitAsync(
            source,
            target,
            () -> attempts.incrementAndGet() == 2,
            cleanups::incrementAndGet).toCompletableFuture();

        assertEquals(1, attempts.get());
        assertFalse(result.isDone());
        source.ready(ZLinkBackendAdmissionKey.channel("other"));
        assertEquals(1, attempts.get());
        source.ready(target);
        assertEquals(2, attempts.get());
        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
        assertEquals(1, cleanups.get());
    }

    @Test
    void readyRacingBetweenFirstFailureAndEnqueueIsPreservedAsOneCredit() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        AtomicInteger attempts = new AtomicInteger();
        ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.channel("orders");

        var result = ZLinkSubmitResults.submitAsync(
            source,
            key,
            () -> {
                int current = attempts.incrementAndGet();
                if (current == 1) {
                    source.ready(key);
                    return false;
                }
                return true;
            },
            () -> { }).toCompletableFuture();

        assertEquals(2, attempts.get());
        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
    }

    @Test
    void oneReadySignalRetriesOnlyOnePendingSubmission() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        AtomicInteger attempts = new AtomicInteger();
        ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.socket();
        var first = ZLinkSubmitResults.submitAsync(
            source, key, () -> attempts.incrementAndGet() >= 3, () -> { })
            .toCompletableFuture();
        var second = ZLinkSubmitResults.submitAsync(
            source, key, () -> attempts.incrementAndGet() >= 4, () -> { })
            .toCompletableFuture();

        assertEquals(2, attempts.get());
        source.ready(key);
        assertEquals(3, attempts.get());
        assertTrue(first.isDone() ^ second.isDone());
        source.ready(key);
        assertEquals(4, attempts.get());
        assertTrue(first.isDone());
        assertTrue(second.isDone());
    }

    @Test
    void sourcePendingCapacityRejectsOnlyAfterEveryCallAttemptsOnce() {
        FakeSource source = new FakeSource(Duration.ofSeconds(30), 1);
        AtomicInteger attempts = new AtomicInteger();
        AtomicInteger cleanups = new AtomicInteger();
        ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.socket();

        var first = ZLinkSubmitResults.submitAsync(
            source, key, () -> {
                attempts.incrementAndGet();
                return false;
            }, cleanups::incrementAndGet).toCompletableFuture();
        var second = ZLinkSubmitResults.submitAsync(
            source, key, () -> {
                attempts.incrementAndGet();
                return false;
            }, cleanups::incrementAndGet).toCompletableFuture();

        assertFalse(first.isDone());
        assertEquals(ZLinkSubmitStatus.BACKPRESSURED, second.join().status());
        assertEquals(2, attempts.get());
        assertEquals(1, cleanups.get());
        assertTrue(first.cancel(false));
        assertEquals(2, cleanups.get());
    }

    @Test
    void cancellationRemovesPendingPayloadAndPreventsLateAdmission() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        AtomicInteger attempts = new AtomicInteger();
        AtomicInteger cleanups = new AtomicInteger();
        ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.socket();
        var result = ZLinkSubmitResults.submitAsync(
            source, key, () -> {
                attempts.incrementAndGet();
                return false;
            }, cleanups::incrementAndGet).toCompletableFuture();

        assertTrue(result.cancel(false));
        source.ready(key);
        assertEquals(1, attempts.get());
        assertEquals(1, cleanups.get());
    }

    @Test
    void familyDeadlineCompletesWithTimedOutAndCleansOnce() throws Exception {
        FakeSource source = new FakeSource(Duration.ofNanos(1));
        AtomicInteger cleanups = new AtomicInteger();
        var result = ZLinkSubmitResults.submitAsync(
            source,
            ZLinkBackendAdmissionKey.socket(),
            () -> false,
            cleanups::incrementAndGet).toCompletableFuture();

        assertEquals(
            ZLinkSubmitStatus.TIMED_OUT,
            result.get(1, TimeUnit.SECONDS).status());
        assertEquals(1, cleanups.get());
        assertEquals(1, ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofNanos(1)));
        assertEquals(1, ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofNanos(999_999)));
        assertEquals(2, ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofNanos(1_000_001)));
        assertEquals(Integer.MAX_VALUE,
            ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofMillis(Integer.MAX_VALUE)));
        assertThrows(IllegalArgumentException.class,
            () -> ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ZERO));
        assertThrows(IllegalArgumentException.class,
            () -> ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofMillis(-1)));
        assertThrows(IllegalArgumentException.class,
            () -> ZLinkAdmissionRuntime.normalizedTimeoutMillis(Duration.ofDays(365)));
        assertThrows(IllegalArgumentException.class,
            () -> ZLinkAdmissionRuntime.normalizedTimeoutMillis(
                Duration.ofMillis((long) Integer.MAX_VALUE + 1L)));
    }

    @Test
    void acceptedCommitCannotBeOverwrittenByCancellation() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        var result = ZLinkSubmitResults.submitAsync(
            source,
            ZLinkBackendAdmissionKey.socket(),
            () -> true,
            () -> { }).toCompletableFuture();

        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
        assertFalse(result.cancel(false));
        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
    }

    @Test
    void payloadCleanupFailureDoesNotOverwriteAcceptedCommit() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));

        var result = ZLinkSubmitResults.submitAsync(
            source,
            ZLinkBackendAdmissionKey.socket(),
            () -> true,
            () -> { throw new IllegalStateException("cleanup failed"); })
            .toCompletableFuture();

        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
    }

    @Test
    void sourceShutdownTerminatesPendingAndRunsReentrantCleanupOutsideLock() {
        FakeSource source = new FakeSource(Duration.ofSeconds(30));
        AtomicInteger cleanups = new AtomicInteger();
        var result = ZLinkSubmitResults.submitAsync(
            source,
            ZLinkBackendAdmissionKey.socket(),
            () -> false,
            () -> {
                cleanups.incrementAndGet();
                source.close();
            }).toCompletableFuture();

        source.close();

        assertEquals(ZLinkSubmitStatus.SHUTDOWN, result.join().status());
        assertEquals(1, cleanups.get());
    }

    @Test
    void disposalAndReadyRaceLeaveOneTerminalAndOneCleanup() throws Exception {
        for (int iteration = 0; iteration < 100; iteration++) {
            FakeSource source = new FakeSource(Duration.ofSeconds(30), 1);
            AtomicInteger attempts = new AtomicInteger();
            AtomicInteger cleanups = new AtomicInteger();
            ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.socket();
            var result = ZLinkSubmitResults.submitAsync(
                source,
                key,
                () -> attempts.incrementAndGet() == 2,
                cleanups::incrementAndGet).toCompletableFuture();
            CountDownLatch start = new CountDownLatch(1);
            Thread ready = Thread.ofVirtual().start(() -> {
                await(start);
                source.ready(key);
            });
            Thread shutdown = Thread.ofVirtual().start(() -> {
                await(start);
                source.close();
            });

            start.countDown();
            ready.join();
            shutdown.join();

            ZLinkSubmitStatus terminal = result.join().status();
            assertTrue(
                terminal == ZLinkSubmitStatus.SUBMITTED
                    || terminal == ZLinkSubmitStatus.SHUTDOWN);
            assertTrue(attempts.get() == 1 || attempts.get() == 2);
            assertEquals(1, cleanups.get());
            source.close();
            source.ready(key);
            assertEquals(1, cleanups.get());
        }
    }

    @Test
    void actorGenerationReadySignalCannotWakeAnotherGeneration() {
        FakeSource source = new FakeSource(Duration.ofSeconds(1));
        AtomicInteger attempts = new AtomicInteger();
        ZLinkBackendAdmissionKey generationSeven = ZLinkBackendAdmissionKey.actor(
            systems.zlink.contracts.core.RoutingId.from("node-a"), "actor-a", 7L);
        ZLinkBackendAdmissionKey generationEight = ZLinkBackendAdmissionKey.actor(
            systems.zlink.contracts.core.RoutingId.from("node-a"), "actor-a", 8L);
        var result = ZLinkSubmitResults.submitAsync(
            source,
            generationSeven,
            () -> attempts.incrementAndGet() == 2,
            () -> { }).toCompletableFuture();

        source.ready(generationEight);
        assertEquals(1, attempts.get());
        assertFalse(result.isDone());

        source.ready(generationSeven);
        assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
        assertEquals(2, attempts.get());
    }

    @Test
    void timeoutTerminalIsNotReplayedWhenTheRouteRecovers() throws Exception {
        FakeSource source = new FakeSource(Duration.ofNanos(1));
        AtomicInteger oldAttempts = new AtomicInteger();
        AtomicInteger newAttempts = new AtomicInteger();
        ZLinkBackendAdmissionKey key = ZLinkBackendAdmissionKey.node(
            systems.zlink.contracts.core.RoutingId.from("node-a"));
        var oldOperation = ZLinkSubmitResults.submitAsync(
            source,
            key,
            () -> {
                oldAttempts.incrementAndGet();
                return false;
            },
            () -> { }).toCompletableFuture();

        assertEquals(ZLinkSubmitStatus.TIMED_OUT,
            oldOperation.get(1, TimeUnit.SECONDS).status());
        source.ready(key);
        var newOperation = ZLinkSubmitResults.submitAsync(
            source,
            key,
            () -> {
                newAttempts.incrementAndGet();
                return true;
            },
            () -> { }).toCompletableFuture();

        assertEquals(ZLinkSubmitStatus.SUBMITTED, newOperation.join().status());
        assertEquals(1, oldAttempts.get());
        assertEquals(1, newAttempts.get());
    }

    @Test
    void timeoutCancellationAndShutdownRaceHasOneWinnerAndOneCleanup() throws Exception {
        for (int iteration = 0; iteration < 100; iteration++) {
            FakeSource source = new FakeSource(Duration.ofMillis(1), 1);
            AtomicInteger attempts = new AtomicInteger();
            AtomicInteger cleanups = new AtomicInteger();
            var result = ZLinkSubmitResults.submitAsync(
                source,
                ZLinkBackendAdmissionKey.socket(),
                () -> {
                    attempts.incrementAndGet();
                    return false;
                },
                cleanups::incrementAndGet).toCompletableFuture();
            CountDownLatch start = new CountDownLatch(1);
            Thread cancellation = Thread.ofVirtual().start(() -> {
                await(start);
                result.cancel(false);
            });
            Thread shutdown = Thread.ofVirtual().start(() -> {
                await(start);
                source.close();
            });

            start.countDown();
            cancellation.join();
            shutdown.join();

            if (!result.isCancelled()) {
                ZLinkSubmitStatus status = result.get(1, TimeUnit.SECONDS).status();
                assertTrue(status == ZLinkSubmitStatus.TIMED_OUT
                    || status == ZLinkSubmitStatus.SHUTDOWN);
            } else {
                assertThrows(CancellationException.class, result::join);
            }
            assertEquals(1, attempts.get());
            assertEquals(1, cleanups.get());
            source.close();
            assertFalse(result.cancel(false));
            assertEquals(1, cleanups.get());
        }
    }

    private static void await(CountDownLatch latch) {
        try {
            latch.await();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new AssertionError(error);
        }
    }

    private static final class FakeSource implements ZLinkBackendObject {
        private final Duration timeout;
        private final int pendingCapacity;
        private Consumer<ZLinkBackendAdmissionKey> ready = ignored -> { };
        private Runnable shutdown = () -> { };

        FakeSource(Duration timeout) {
            this(timeout, ZLinkBackendObject.DEFAULT_PENDING_ADMISSION_CAPACITY);
        }

        FakeSource(Duration timeout, int pendingCapacity) {
            this.timeout = timeout;
            this.pendingCapacity = pendingCapacity;
        }

        @Override public String name() { return "fake"; }
        @Override public void close() { shutdown.run(); }
        @Override public Duration admissionTimeout() { return timeout; }
        @Override public int admissionPendingCapacity() { return pendingCapacity; }
        @Override public void setAdmissionReadyHandler(
            Consumer<ZLinkBackendAdmissionKey> handler) {
            ready = handler;
        }
        @Override public void setAdmissionShutdownHandler(Runnable handler) {
            shutdown = handler;
        }

        void ready(ZLinkBackendAdmissionKey key) {
            ready.accept(key);
        }
    }

}
