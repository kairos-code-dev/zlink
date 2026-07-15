package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkWorkerFailedException;
import systems.zlink.framework.errors.ZLinkWorkerQueueFullException;
import systems.zlink.framework.errors.ZLinkWorkerTimeoutException;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.spots.ZLinkSpotContext;

class DefaultZLinkWorkerCallTest {
    private ZLinkWorkerPool pool;

    @AfterEach
    void closePool() {
        if (pool != null) {
            pool.close();
        }
    }

    @Test
    void submitCompletesWithWorkerResult() throws Exception {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        AtomicReference<String> workerThread = new AtomicReference<>();

        Integer result = new DefaultZLinkWorkerCall<>(pool, () -> {
            workerThread.set(Thread.currentThread().getName());
            return 42;
        }).submit().toCompletableFuture().get(5, TimeUnit.SECONDS);

        assertEquals(42, result);
        assertTrue(workerThread.get().startsWith("zlink-worker-"));
        assertNotEquals(Thread.currentThread().getName(), workerThread.get());
    }

    @Test
    void queueFullFailsWithoutBlocking() throws Exception {
        pool = new ZLinkWorkerPool(0, 1, Duration.ofSeconds(30), 1);
        CountDownLatch release = new CountDownLatch(1);
        try {
            new DefaultZLinkWorkerCall<>(pool, () -> {
                release.await();
                return 0;
            }).submit();
            awaitCondition(() -> pool.poolSize() == 1);
            new DefaultZLinkWorkerCall<>(pool, () -> 0).submit();
            awaitCondition(() -> pool.queueLength() == 1);

            CompletableFuture<Integer> overflow =
                new DefaultZLinkWorkerCall<Integer>(pool, () -> 0)
                    .submit().toCompletableFuture();
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
        DefaultZLinkWorkerCall<Integer> call = new DefaultZLinkWorkerCall<>(pool, () -> {
            release.await();
            return 7;
        });
        call.timeout(Duration.ofMillis(100));
        CompletableFuture<Integer> result = call.submit().toCompletableFuture();
        ExecutionException failure = assertThrows(
            ExecutionException.class,
            () -> result.get(5, TimeUnit.SECONDS));
        assertInstanceOf(ZLinkWorkerTimeoutException.class, failure.getCause());

        release.countDown();
        Thread.sleep(200);
        assertTrue(result.isCompletedExceptionally());
    }

    @Test
    void workerExceptionMapsToWorkerFailure() {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        CompletableFuture<Integer> result =
            new DefaultZLinkWorkerCall<Integer>(pool, () -> {
                throw new IllegalStateException("boom");
            }).submit().toCompletableFuture();

        CompletionException failure = assertThrows(CompletionException.class, result::join);
        assertInstanceOf(ZLinkWorkerFailedException.class, failure.getCause());
        assertInstanceOf(IllegalStateException.class, failure.getCause().getCause());
    }

    @Test
    void secondSubmitThrows() {
        pool = new ZLinkWorkerPool(0, 2, Duration.ofSeconds(30), 16);
        DefaultZLinkWorkerCall<Integer> call = new DefaultZLinkWorkerCall<>(pool, () -> 1);
        call.submit();
        assertThrows(IllegalStateException.class, call::submit);
    }

    @Test
    void workerSurfaceSeparatesCpuAndIoWithoutLegacyEntryPoint() throws Exception {
        assertEquals(1, ZLinkSpotContext.class.getMethods().length > 0
            ? java.util.Arrays.stream(ZLinkSpotContext.class.getMethods())
                .filter(method -> method.getName().equals("runCpuWorker")).count()
            : 0);
        assertEquals(1, java.util.Arrays.stream(ZLinkSpotContext.class.getMethods())
            .filter(method -> method.getName().equals("runIoWorker")).count());
        assertEquals(0, java.util.Arrays.stream(ZLinkSpotContext.class.getMethods())
            .filter(method -> method.getName().equals("runWorker")).count());
    }

    @Test
    void ioWorkerDoesNotOccupyBoundedCpuPool() throws Exception {
        pool = new ZLinkWorkerPool(0, 1, Duration.ofSeconds(30), 1);
        CompletableFuture<Integer> pending = new CompletableFuture<>();

        CompletableFuture<Integer> result = new DefaultZLinkIoWorkerCall<>(pool, () -> pending)
            .submit().toCompletableFuture();

        assertEquals(0, pool.poolSize());
        assertEquals(0, pool.queueLength());
        pending.complete(42);
        assertEquals(42, result.get(5, TimeUnit.SECONDS));
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
