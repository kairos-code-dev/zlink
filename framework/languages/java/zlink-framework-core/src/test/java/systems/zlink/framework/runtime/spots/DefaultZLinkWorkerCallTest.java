package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Function;
import java.util.function.Supplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkOperationCanceledException;
import systems.zlink.framework.errors.ZLinkWorkerFailedException;
import systems.zlink.framework.errors.ZLinkWorkerQueueFullException;
import systems.zlink.framework.errors.ZLinkWorkerTimeoutException;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.execution.ZLinkWorkerPool;

class DefaultZLinkWorkerCallTest {
    private final ZLinkAsyncSerialQueue spotQueue = new ZLinkAsyncSerialQueue();
    private final java.util.concurrent.ExecutorService callbackExecutor =
        java.util.concurrent.Executors.newVirtualThreadPerTaskExecutor();
    private ZLinkWorkerPool pool;

    @AfterEach
    void closePool() {
        if (pool != null) {
            pool.close();
        }
        callbackExecutor.shutdownNow();
    }

    private Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>> postToQueue() {
        // Mirror the runtime wiring: queue for ordering plus an executor hop so
        // the callback never runs inline on the enqueuing (worker) thread.
        return operation -> spotQueue.enqueue(() ->
            CompletableFuture
                .supplyAsync(operation, callbackExecutor)
                .thenCompose(stage -> stage));
    }

    @Test
    void awaitableTerminatorCompletesWithWorkerResult() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        AtomicReference<String> workerThread = new AtomicReference<>();

        Integer result = new DefaultZLinkWorkerCall<>(
            pool,
            token -> {
                workerThread.set(Thread.currentThread().getName());
                return 42;
            },
            postToQueue())
            .submit()
            .toCompletableFuture()
            .get(5, TimeUnit.SECONDS);

        assertEquals(42, result);
        assertTrue(workerThread.get().startsWith("zlink-worker-"));
        assertNotEquals(Thread.currentThread().getName(), workerThread.get());
    }

    @Test
    void callbackTerminatorRunsOnSpotQueueNotWorkerThread() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        AtomicReference<String> workerThread = new AtomicReference<>();
        CompletableFuture<String> callbackThread = new CompletableFuture<>();

        new DefaultZLinkWorkerCall<>(
            pool,
            token -> {
                workerThread.set(Thread.currentThread().getName());
                return 7;
            },
            postToQueue())
            .submit(
                (value, token) -> callbackThread.complete(Thread.currentThread().getName()),
                (error, token) -> callbackThread.completeExceptionally(error));

        String observed = callbackThread.get(5, TimeUnit.SECONDS);
        assertNotEquals(workerThread.get(), observed);
    }

    @Test
    void queueFullFailsImmediatelyWithoutBlocking() throws Exception {
        pool = new ZLinkWorkerPool(0, 1, Duration.ofSeconds(30), 1);
        CountDownLatch release = new CountDownLatch(1);
        try {
            // Occupy the single thread, then the single queue slot.
            new DefaultZLinkWorkerCall<>(pool, token -> {
                release.await();
                return 0;
            }, postToQueue()).submit();
            awaitCondition(() -> pool.poolSize() == 1);
            new DefaultZLinkWorkerCall<>(pool, token -> 0, postToQueue()).submit();
            awaitCondition(() -> pool.queueLength() == 1);

            CompletableFuture<Integer> overflow =
                new DefaultZLinkWorkerCall<Integer>(pool, token -> 0, postToQueue())
                    .submit()
                    .toCompletableFuture();
            ExecutionException failure = assertThrows(
                ExecutionException.class,
                () -> overflow.get(5, TimeUnit.SECONDS));
            assertInstanceOf(ZLinkWorkerQueueFullException.class, failure.getCause());
        } finally {
            release.countDown();
        }
    }

    @Test
    void timeoutFailsCallerAndDropsLateCompletion() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        CountDownLatch release = new CountDownLatch(1);
        ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();
        CompletableFuture<Throwable> failed = new CompletableFuture<>();
        AtomicReference<Boolean> tokenCancelled = new AtomicReference<>(false);

        DefaultZLinkWorkerCall<Integer> call = new DefaultZLinkWorkerCall<>(
            pool,
            token -> {
                release.await();
                tokenCancelled.set(token.isCancellationRequested());
                return 7;
            },
            postToQueue());
        call.timeout(Duration.ofMillis(100));
        call.submit(
            (value, token) -> events.add("completed:" + value),
            (error, token) -> {
                events.add("error:" + error.getClass().getSimpleName());
                failed.complete(error);
            });

        assertInstanceOf(
            ZLinkWorkerTimeoutException.class,
            failed.get(5, TimeUnit.SECONDS));

        // Let the abandoned work finish; the late result must be dropped.
        release.countDown();
        awaitCondition(() -> tokenCancelled.get() != null && tokenCancelled.get());
        Thread.sleep(200);
        assertEquals(1, events.size());
        assertFalse(events.stream().anyMatch(e -> e.startsWith("completed:")));
        assertTrue(tokenCancelled.get(), "work token must observe the timeout cancellation");
    }

    @Test
    void workerExceptionMapsToWorkerFailedWithCause() {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);

        CompletableFuture<Integer> result =
            new DefaultZLinkWorkerCall<Integer>(pool, token -> {
                throw new IllegalStateException("boom");
            }, postToQueue())
                .submit()
                .toCompletableFuture();

        CompletionException failure = assertThrows(
            CompletionException.class,
            result::join);
        assertInstanceOf(ZLinkWorkerFailedException.class, failure.getCause());
        assertInstanceOf(IllegalStateException.class, failure.getCause().getCause());
    }

    @Test
    void idleThreadsShrinkAfterIdleTimeout() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofMillis(150), 16);

        new DefaultZLinkWorkerCall<>(pool, token -> 1, postToQueue())
            .submit()
            .toCompletableFuture()
            .get(5, TimeUnit.SECONDS);
        assertTrue(pool.poolSize() >= 1);

        awaitCondition(() -> pool.poolSize() == 0);
        assertEquals(0, pool.poolSize());
    }

    @Test
    void detachedCallbackObservesStateMutatedWhileWaiting() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        CountDownLatch release = new CountDownLatch(1);
        AtomicReference<String> spotState = new AtomicReference<>("initial");
        CompletableFuture<String> observed = new CompletableFuture<>();

        new DefaultZLinkWorkerCall<>(pool, token -> {
            release.await();
            return 1;
        }, postToQueue())
            .submit(
                (value, token) -> observed.complete(spotState.get()),
                (error, token) -> observed.completeExceptionally(error));

        // Another serial-line callback mutates the state before completion.
        spotQueue.enqueue(() -> {
            spotState.set("mutated");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture().get(5, TimeUnit.SECONDS);
        release.countDown();

        assertEquals("mutated", observed.get(5, TimeUnit.SECONDS));
    }

    @Test
    void yieldAllowsOtherSpotQueueWorkWhileWorkerRuns() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        CountDownLatch releaseWork = new CountDownLatch(1);
        CompletableFuture<Void> workerStarted = new CompletableFuture<>();
        CompletableFuture<Void> firstResumed = new CompletableFuture<>();
        CompletableFuture<Void> releaseFirstResume = new CompletableFuture<>();
        ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();

        CompletableFuture<Void> first = spotQueue.enqueue(() -> {
            events.add("first-start");
            Integer result = new DefaultZLinkWorkerCall<>(
                pool,
                token -> {
                    workerStarted.complete(null);
                    releaseWork.await();
                    return 42;
                },
                postToQueue())
                .yield();
            events.add("first-resumed:" + result);
            firstResumed.complete(null);
            releaseFirstResume.join();
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        workerStarted.get(5, TimeUnit.SECONDS);

        spotQueue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture().get(5, TimeUnit.SECONDS);

        assertEquals(List.of("first-start", "second-start"), List.copyOf(events));

        releaseWork.countDown();
        firstResumed.get(5, TimeUnit.SECONDS);

        CompletableFuture<Void> third = spotQueue.enqueue(() -> {
            events.add("third-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        assertFalse(third.isDone());

        releaseFirstResume.complete(null);
        third.get(5, TimeUnit.SECONDS);
        first.get(5, TimeUnit.SECONDS);
        assertEquals(List.of(
            "first-start",
            "second-start",
            "first-resumed:42",
            "third-start"), List.copyOf(events));
    }

    @Test
    void cancellationAwareYieldReleasesQueueAndDropsLateWorkerResult() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        AtomicBoolean canceled = new AtomicBoolean(false);
        CountDownLatch releaseWork = new CountDownLatch(1);
        CompletableFuture<Void> workerStarted = new CompletableFuture<>();
        CompletableFuture<Void> firstCanceled = new CompletableFuture<>();
        ConcurrentLinkedQueue<String> events = new ConcurrentLinkedQueue<>();

        CompletableFuture<Void> first = spotQueue.enqueue(() -> {
            events.add("first-start");
            try {
                new DefaultZLinkWorkerCall<>(
                    pool,
                    token -> {
                        workerStarted.complete(null);
                        releaseWork.await();
                        return 1;
                    },
                    postToQueue())
                    .yield(canceled::get);
                events.add("first-unexpected-resumed");
            } catch (CompletionException error) {
                assertInstanceOf(ZLinkOperationCanceledException.class, error.getCause());
                events.add("first-canceled");
                firstCanceled.complete(null);
            }
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        workerStarted.get(5, TimeUnit.SECONDS);
        CompletableFuture<Void> second = spotQueue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        second.get(5, TimeUnit.SECONDS);

        canceled.set(true);
        firstCanceled.get(5, TimeUnit.SECONDS);
        releaseWork.countDown();
        first.get(5, TimeUnit.SECONDS);

        assertEquals(List.of("first-start", "second-start", "first-canceled"), List.copyOf(events));
        assertFalse(events.contains("first-unexpected-resumed"));
    }

    @Test
    void yieldRequiresCapturedTurn() {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);

        DefaultZLinkWorkerCall<Integer> call =
            new DefaultZLinkWorkerCall<>(pool, token -> 1, postToQueue());

        IllegalStateException error = assertThrows(
            IllegalStateException.class,
            call::yield);

        assertTrue(error.getMessage().contains("captured"));
    }

    @Test
    void secondTerminatorThrows() {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);

        DefaultZLinkWorkerCall<Integer> call =
            new DefaultZLinkWorkerCall<>(pool, token -> 1, postToQueue());
        call.submit();
        assertThrows(
            IllegalStateException.class,
            () -> call.submit((value, token) -> { }, (error, token) -> { }));
    }

    private static void awaitCondition(Supplier<Boolean> condition) throws InterruptedException {
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (!condition.get()) {
            if (System.nanoTime() > deadline) {
                throw new AssertionError("condition was not reached in time");
            }
            Thread.sleep(10);
        }
    }
}
