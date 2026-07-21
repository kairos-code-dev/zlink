package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.service.spot.PublishDetail;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.channels.ZLinkPublishResult;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.configuration.ZLinkMessageFlowLogMode;
import systems.zlink.framework.runtime.configuration.ZLinkDispatchOptionsRegistration;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.runtime.messaging.ZLinkStringMessageSerializer;

final class ZLinkSpotPublisherRuntimeTest {
    @Test
    void remoteCapacityDropReturnsBackpressuredAndPreservesPartialDetail() {
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            return new PublishDetail(2, 1, 1, 0, 1, 1, 0);
        });
        try (runtime) {
            ZLinkPublishResult result = submit(runtime, "partial").join();

            assertEquals(ZLinkSubmitStatus.BACKPRESSURED, result.status());
            assertEquals(2, result.detail().snapshotRemoteNodeCount());
            assertEquals(1, result.detail().admittedRemoteNodeCount());
            assertEquals(1, result.detail().droppedRemoteNodeCount());
            assertEquals(1, result.detail().admittedLocalSpotCount());
            assertEquals(1, coreCalls.get());
        }
    }

    @Test
    void allUnreachableRemoteSnapshotRemainsSubmittedWithExactDetail() {
        ZLinkSpotPublisherRuntime runtime = runtime(
            () -> new PublishDetail(2, 0, 0, 2, 0, 0, 0));
        try (runtime) {
            ZLinkPublishResult result = submit(runtime, "unreachable").join();

            assertEquals(ZLinkSubmitStatus.SUBMITTED, result.status());
            assertEquals(2, result.detail().snapshotRemoteNodeCount());
            assertEquals(0, result.detail().admittedRemoteNodeCount());
            assertEquals(2, result.detail().unreachableRemoteNodeCount());
        }
    }

    @Test
    void localOnlyCapacityDropDoesNotChangeRemotePublishAdmission() {
        ZLinkSpotPublisherRuntime runtime = runtime(
            () -> new PublishDetail(0, 0, 0, 0, 1, 0, 1));
        try (runtime) {
            ZLinkPublishResult result = submit(runtime, "local-drop").join();

            assertEquals(ZLinkSubmitStatus.SUBMITTED, result.status());
            assertEquals(1, result.detail().snapshotLocalSpotCount());
            assertEquals(1, result.detail().droppedLocalSpotCount());
        }
    }

    @Test
    void emptyReadySnapshotPreservesCoreTargetNotFound() {
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            throw new ZlinkSubmitException(SubmitResult.NOT_FOUND);
        });
        try (runtime) {
            ZLinkPublishResult result = submit(runtime, "missing").join();

            assertEquals(ZLinkSubmitStatus.TARGET_NOT_FOUND, result.status());
            assertEquals(0, result.detail().snapshotRemoteNodeCount());
            assertEquals(0, result.detail().snapshotLocalSpotCount());
            assertEquals(1, coreCalls.get());
        }
    }

    @Test
    void cancellationAfterCommitDoesNotReplaceTheCorePublishResult() throws Exception {
        CountDownLatch committed = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            committed.countDown();
            await(release);
            return new PublishDetail(1, 1, 0, 0, 0, 0, 0);
        });
        try (runtime) {
            CompletableFuture<ZLinkPublishResult> result = submit(runtime, "committed");
            assertTrue(committed.await(1, TimeUnit.SECONDS));

            assertFalse(result.cancel(false));
            release.countDown();

            assertEquals(ZLinkSubmitStatus.SUBMITTED, result.join().status());
            assertEquals(1, coreCalls.get());
        } finally {
            release.countDown();
        }
    }

    @Test
    void shutdownAfterCommitDoesNotReplaceTheCorePublishResult() throws Exception {
        CountDownLatch committed = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            committed.countDown();
            awaitUninterruptibly(release);
            return new PublishDetail(1, 1, 0, 0, 0, 0, 0);
        });
        try {
            CompletableFuture<ZLinkPublishResult> result = submit(runtime, "committed");
            assertTrue(committed.await(1, TimeUnit.SECONDS));

            runtime.close();
            release.countDown();

            assertEquals(ZLinkSubmitStatus.SUBMITTED,
                result.get(1, TimeUnit.SECONDS).status());
            assertEquals(1, coreCalls.get());
        } finally {
            release.countDown();
            runtime.close();
        }
    }

    @Test
    void directHandoffRejectsWhenEveryMulticastWorkerIsBusy() throws Exception {
        int workerCount = Math.max(2, Runtime.getRuntime().availableProcessors());
        CountDownLatch started = new CountDownLatch(workerCount);
        CountDownLatch release = new CountDownLatch(1);
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            started.countDown();
            await(release);
            return new PublishDetail(1, 1, 0, 0, 0, 0, 0);
        });
        List<CompletableFuture<ZLinkPublishResult>> committed = new ArrayList<>();
        try (runtime) {
            for (int index = 0; index < workerCount; index++) {
                committed.add(submit(runtime, "held-" + index));
            }
            assertTrue(started.await(2, TimeUnit.SECONDS));

            ZLinkPublishResult rejected = submit(runtime, "rejected").join();

            assertEquals(ZLinkSubmitStatus.BACKPRESSURED, rejected.status());
            assertEquals(workerCount, coreCalls.get());
            release.countDown();
            committed.forEach(CompletableFuture::join);
        } finally {
            release.countDown();
        }
    }

    @Test
    void closedRuntimeRejectsNewPublishAsShutdownWithoutCoreCall() {
        AtomicInteger coreCalls = new AtomicInteger();
        ZLinkSpotPublisherRuntime runtime = runtime(() -> {
            coreCalls.incrementAndGet();
            return new PublishDetail(0, 0, 0, 0, 0, 0, 0);
        });
        runtime.close();

        ZLinkPublishResult result = submit(runtime, "after-close").join();

        assertEquals(ZLinkSubmitStatus.SHUTDOWN, result.status());
        assertEquals(0, coreCalls.get());
    }

    private static CompletableFuture<ZLinkPublishResult> submit(
        ZLinkSpotPublisherRuntime runtime,
        String payload) {
        return runtime.submitAsync(
                "mesh",
                "channel",
                "topic",
                Message.from(payload.getBytes(StandardCharsets.UTF_8)),
                Optional.empty(),
                ZLinkApplicationMetadata.empty())
            .toCompletableFuture();
    }

    private static ZLinkSpotPublisherRuntime runtime(Supplier<PublishDetail> publish) {
        ZLinkStringMessageSerializer serializer = new ZLinkStringMessageSerializer();
        ZLinkDispatchOptionsRegistration options = new ZLinkDispatchOptionsRegistration();
        options.messageFlow(ZLinkMessageFlowLogMode.OFF);
        ZLinkSpotPublisherRuntime runtime = new ZLinkSpotPublisherRuntime(
            serializer,
            new ZLinkSpotRouteMessages(serializer),
            new ZLinkMessageFlowTracer(
                options,
                ZLinkHandlerActivator.reflection(),
                Runnable::run));
        AtomicInteger proxyClose = new AtomicInteger();
        ZLinkInternalSpotNode node = (ZLinkInternalSpotNode) Proxy.newProxyInstance(
            ZLinkInternalSpotNode.class.getClassLoader(),
            new Class<?>[] {ZLinkInternalSpotNode.class},
            (ignored, method, arguments) -> switch (method.getName()) {
                case "publishDetailed" -> publish.get();
                case "name" -> "publisher-node";
                case "close" -> {
                    proxyClose.incrementAndGet();
                    yield null;
                }
                default -> defaultValue(method.getReturnType());
            });
        runtime.register("mesh", node);
        return runtime;
    }

    private static Object defaultValue(Class<?> type) {
        if (!type.isPrimitive()) {
            return null;
        }
        if (type == boolean.class) {
            return false;
        }
        if (type == char.class) {
            return '\0';
        }
        return 0;
    }

    private static void await(CountDownLatch latch) {
        try {
            latch.await();
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new AssertionError(error);
        }
    }

    private static void awaitUninterruptibly(CountDownLatch latch) {
        boolean interrupted = false;
        while (true) {
            try {
                latch.await();
                break;
            } catch (InterruptedException ignored) {
                interrupted = true;
            }
        }
        if (interrupted) {
            Thread.currentThread().interrupt();
        }
    }
}
