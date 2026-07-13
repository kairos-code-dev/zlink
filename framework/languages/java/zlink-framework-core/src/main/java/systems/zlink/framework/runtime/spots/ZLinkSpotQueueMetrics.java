package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.internal.metrics.ZLinkRuntimeMetrics;

final class ZLinkSpotQueueMetrics {
    private ZLinkSpotQueueMetrics() { }

    static CompletionStage<Void> enqueue(
        ZLinkAsyncSerialQueue queue,
        String kind,
        Supplier<CompletionStage<Void>> operation) {
        if (!ZLinkRuntimeMetrics.enabled()) {
            return queue.enqueue(operation);
        }
        long enqueued = System.nanoTime();
        Map<String, String> tags = Map.of("kind", kind);
        ZLinkRuntimeMetrics.add("zlink.spot.queue.depth", 1, tags);
        return queue.enqueue(() -> {
            ZLinkRuntimeMetrics.add("zlink.spot.queue.depth", -1, tags);
            ZLinkRuntimeMetrics.record("zlink.spot.queue.wait.duration",
                Duration.ofNanos(System.nanoTime() - enqueued), tags);
            return operation.get();
        });
    }
}
