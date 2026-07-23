package systems.zlink.framework.execution;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.configuration.ZLinkFlowOrigin;
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext;

final class ZLinkAsyncSerialQueueTest {
    @Test
    void submitKeepsTurnUntilIncompleteStageCompletes() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> firstGate = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            firstStarted.complete(null);
            return firstGate
                .thenRun(() -> events.add("first-complete"));
        }).toCompletableFuture();
        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        firstStarted.get(3, TimeUnit.SECONDS);
        assertFalse(second.isDone());
        assertEquals(List.of("first-start"), events);
        assertFalse(first.isDone());

        firstGate.complete(null);

        first.get(3, TimeUnit.SECONDS);
        second.get(3, TimeUnit.SECONDS);
        assertEquals(List.of("first-start", "first-complete", "second-start"), events);
    }

    @Test
    void boundedQueueSignalsWhenOnePendingSlotBecomesAvailable() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue(false, 1);
        CompletableFuture<Void> firstGate = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        CompletableFuture<Void> capacityAvailable = new CompletableFuture<>();
        queue.onCapacityAvailable(() -> capacityAvailable.complete(null));

        assertTrue(queue.tryEnqueue(() -> {
            firstStarted.complete(null);
            return firstGate;
        }));
        firstStarted.get(3, TimeUnit.SECONDS);
        assertTrue(queue.tryEnqueue(() -> CompletableFuture.completedFuture(null)));
        assertFalse(queue.tryEnqueue(() -> CompletableFuture.completedFuture(null)));

        firstGate.complete(null);

        capacityAvailable.get(3, TimeUnit.SECONDS);
        assertTrue(queue.tryEnqueue(() -> CompletableFuture.completedFuture(null)));
    }

    @Test
    void yieldReleasesWaitingTurnAndReentersContinuationInQueueOrder() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> firstGate = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            firstStarted.complete(null);
            return ZLinkAsyncSerialQueue.yieldCurrent(firstGate)
                .thenRun(() -> events.add("first-complete"));
        }).toCompletableFuture();
        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        firstStarted.get(3, TimeUnit.SECONDS);
        second.get(3, TimeUnit.SECONDS);
        assertEquals(List.of("first-start", "second-start"), events);
        assertFalse(first.isDone());

        firstGate.complete(null);

        first.get(3, TimeUnit.SECONDS);
        assertEquals(List.of("first-start", "second-start", "first-complete"), events);
    }

    @Test
    void yieldRetainsTurnContextAcrossHandlerExecutor() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> remote = new CompletableFuture<>();
        CompletableFuture<Void> handlerStarted = new CompletableFuture<>();
        CompletableFuture<Void> probeStarted = new CompletableFuture<>();
        try (var handlerExecutor = java.util.concurrent.Executors.newSingleThreadExecutor()) {
            CompletableFuture<Void> first = queue.enqueue(() -> {
                CompletableFuture<Void> result = new CompletableFuture<>();
                ZLinkAsyncSerialQueue.propagateCurrent(handlerExecutor).execute(() -> {
                    handlerStarted.complete(null);
                    ZLinkAsyncSerialQueue.yieldCurrent(remote)
                        .whenComplete((ignored, error) -> result.complete(null));
                });
                return result;
            }).toCompletableFuture();
            queue.enqueue(() -> {
                probeStarted.complete(null);
                return CompletableFuture.completedFuture(null);
            });

            handlerStarted.get(3, TimeUnit.SECONDS);
            probeStarted.get(3, TimeUnit.SECONDS);
            assertFalse(first.isDone());
            remote.complete(null);
            first.get(3, TimeUnit.SECONDS);
        }
    }

    @Test
    void continuesAfterPreviousFailure() {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        List<String> events = new ArrayList<>();

        queue.enqueue(() -> {
            events.add("first-start");
            return CompletableFuture.failedFuture(new IllegalStateException("boom"));
        });
        queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture().join();

        assertEquals(List.of("first-start", "second-start"), events);
    }

    @Test
    void reentersManagedContinuationWithItsCapturedFlow() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> gate = new CompletableFuture<>();
        CompletableFuture<Void> started = new CompletableFuture<>();
        CompletableFuture<String> observed = new CompletableFuture<>();
        ZLinkFlowContext.State flow = ZLinkFlowContext.create(ZLinkFlowOrigin.INBOUND);

        queue.enqueue(() -> {
            try (ZLinkFlowContext.Scope ignored = ZLinkFlowContext.enter(flow)) {
                started.complete(null);
                return ZLinkAsyncSerialQueue.yieldCurrent(gate)
                    .thenRun(() -> observed.complete(ZLinkFlowContext.current().flowId()));
            }
        });

        started.get(3, TimeUnit.SECONDS);
        gate.complete(null);

        assertEquals(flow.flowId(), observed.get(3, TimeUnit.SECONDS));
    }

    @Test
    void startsQueuedOperationWithFlowCapturedAtEnqueue() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<String> observed = new CompletableFuture<>();
        ZLinkFlowContext.State flow = ZLinkFlowContext.create(ZLinkFlowOrigin.INBOUND);

        try (ZLinkFlowContext.Scope ignored = ZLinkFlowContext.enter(flow)) {
            queue.enqueue(() -> {
                observed.complete(ZLinkFlowContext.current().flowId());
                return CompletableFuture.completedFuture(null);
            });
        }

        assertEquals(flow.flowId(), observed.get(3, TimeUnit.SECONDS));
    }

    @Test
    void retainedTurnContinuationCanExplicitlyYield() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> remote = new CompletableFuture<>();
        CompletableFuture<Void> afterYield = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        CompletableFuture<Void> secondStarted = new CompletableFuture<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            firstStarted.complete(null);
            return ZLinkAsyncSerialQueue.manageCurrent(remote)
                .thenCompose(ignored -> ZLinkAsyncSerialQueue.yieldCurrent(afterYield));
        }).toCompletableFuture();
        queue.enqueue(() -> {
            secondStarted.complete(null);
            return CompletableFuture.completedFuture(null);
        });

        firstStarted.get(3, TimeUnit.SECONDS);
        CompletableFuture.runAsync(() -> remote.complete(null)).join();
        secondStarted.get(3, TimeUnit.SECONDS);
        assertFalse(first.isDone());
        afterYield.complete(null);
        first.get(3, TimeUnit.SECONDS);
    }

    @Test
    void relocationSealHoldsIngressAndAbortRestoresArrivalOrder()
        throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> sealNow = new CompletableFuture<>();
        CompletableFuture<Void> intentStarted = new CompletableFuture<>();
        CompletableFuture<ZLinkAsyncSerialQueue.RelocationSeal> sealed =
            new CompletableFuture<>();
        List<String> handled =
            new java.util.concurrent.CopyOnWriteArrayList<>();

        queue.enqueue(() -> {
            intentStarted.complete(null);
            sealNow.join();
            sealed.complete(queue.trySealRelocation().orElseThrow());
            return CompletableFuture.completedFuture(null);
        });
        intentStarted.get(3, TimeUnit.SECONDS);
        queue.enqueueRelocatable(
            new byte[] {1},
            () -> {
                handled.add("one");
                return CompletableFuture.completedFuture(null);
            });
        queue.enqueueRelocatable(
            new byte[] {2},
            () -> {
                handled.add("two");
                return CompletableFuture.completedFuture(null);
        });
        sealNow.complete(null);
        ZLinkAsyncSerialQueue.RelocationSeal seal =
            sealed.get(3, TimeUnit.SECONDS);

        queue.enqueueRelocatable(
            new byte[] {3},
            () -> {
                handled.add("three");
                return CompletableFuture.completedFuture(null);
            });
        CompletableFuture<Void> infrastructure = queue.enqueue(() -> {
            handled.add("infrastructure");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        infrastructure.get(3, TimeUnit.SECONDS);
        assertEquals(List.of("infrastructure"), handled);
        assertEquals(2, seal.captured().size());

        assertTrue(queue.abortRelocation(seal));
        waitForSize(handled, 4);
        assertEquals(
            List.of("infrastructure", "one", "two", "three"),
            handled);
    }

    @Test
    void relocationCommitReturnsOnlyHeldIngressAndRejectsNewOwnerWork()
        throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> sealNow = new CompletableFuture<>();
        CompletableFuture<Void> intentStarted = new CompletableFuture<>();
        CompletableFuture<ZLinkAsyncSerialQueue.RelocationSeal> sealed =
            new CompletableFuture<>();
        AtomicReference<Boolean> ran = new AtomicReference<>(false);

        queue.enqueue(() -> {
            intentStarted.complete(null);
            sealNow.join();
            sealed.complete(queue.trySealRelocation().orElseThrow());
            return CompletableFuture.completedFuture(null);
        });
        intentStarted.get(3, TimeUnit.SECONDS);
        CompletableFuture<Void> captured = queue.enqueueRelocatable(
            new byte[] {1},
            () -> {
                ran.set(true);
                return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        sealNow.complete(null);
        ZLinkAsyncSerialQueue.RelocationSeal seal =
            sealed.get(3, TimeUnit.SECONDS);
        CompletableFuture<Void> held = queue.enqueueRelocatable(
            new byte[] {2},
            () -> {
                ran.set(true);
                return CompletableFuture.completedFuture(null);
            }).toCompletableFuture();

        List<ZLinkAsyncSerialQueue.QueuedRecord> relay =
            queue.commitRelocation(seal).orElseThrow();
        captured.get(3, TimeUnit.SECONDS);
        held.get(3, TimeUnit.SECONDS);

        assertFalse(ran.get());
        assertEquals(1, relay.size());
        assertArrayEquals(new byte[] {2}, relay.getFirst().payload());
        assertTrue(queue.enqueueRelocatable(
            new byte[] {3},
            () -> CompletableFuture.completedFuture(null))
            .toCompletableFuture()
            .isCompletedExceptionally());
    }

    @Test
    void relocationCommitReleasesSourceResourcesWithoutRunningHandler()
        throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        AtomicReference<Boolean> released = new AtomicReference<>(false);
        AtomicReference<Boolean> ran = new AtomicReference<>(false);
        ZLinkAsyncSerialQueue.RelocationSeal seal =
            queue.trySealRelocation().orElseThrow();
        CompletableFuture<Void> held = queue.enqueueRelocatable(
            new byte[] {7},
            () -> {
                ran.set(true);
                return CompletableFuture.completedFuture(null);
            },
            () -> released.set(true)).toCompletableFuture();

        assertEquals(1, queue.commitRelocation(seal).orElseThrow().size());
        held.get(3, TimeUnit.SECONDS);
        assertTrue(released.get());
        assertFalse(ran.get());
    }

    private static void waitForSize(
        List<String> values,
        int expected) throws Exception {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
        while (values.size() < expected && System.nanoTime() < deadline) {
            Thread.sleep(1);
        }
        assertEquals(expected, values.size());
    }
}
