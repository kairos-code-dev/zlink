package systems.zlink.framework.execution;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;

final class ZLinkAsyncSerialQueueTest {
    @Test
    void enqueue_startsNextOperationOnlyAfterPreviousStageCompletes() {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> firstGate = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            return firstGate;
        }).toCompletableFuture();
        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        assertEquals(List.of("first-start"), events);
        assertFalse(first.isDone());
        assertFalse(second.isDone());

        firstGate.complete(null);

        assertEquals(List.of("first-start", "second-start"), events);
        assertEquals(null, first.join());
        assertEquals(null, second.join());
    }

    @Test
    void enqueue_continuesAfterPreviousFailure() {
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
}
