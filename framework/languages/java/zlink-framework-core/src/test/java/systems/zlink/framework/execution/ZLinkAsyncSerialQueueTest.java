package systems.zlink.framework.execution;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.ZLinkAwait;
import org.junit.jupiter.api.Test;

final class ZLinkAsyncSerialQueueTest {
    @Test
    void enqueue_startsNextOperationOnlyAfterPreviousStageCompletes() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<Void> firstGate = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            firstStarted.complete(null);
            return firstGate;
        }).toCompletableFuture();
        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        firstStarted.get(3, TimeUnit.SECONDS);
        assertEquals(List.of("first-start"), events);
        assertFalse(first.isDone());
        assertFalse(second.isDone());

        firstGate.complete(null);

        second.get(3, TimeUnit.SECONDS);
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

    @Test
    void yield_releasesGateUntilOperationCompletes() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<String> io = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        CompletableFuture<Void> firstResumed = new CompletableFuture<>();
        CompletableFuture<Void> releaseFirstResume = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            firstStarted.complete(null);
            String result = ZLinkYieldTurn.current().awaitFrameworkCallBlocking(io);
            events.add("first-resumed:" + result);
            firstResumed.complete(null);
            ZLinkAwait.awaitVoid(releaseFirstResume);
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        firstStarted.get(3, TimeUnit.SECONDS);

        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        second.get(3, TimeUnit.SECONDS);

        assertEquals(List.of("first-start", "second-start"), events);

        io.complete("ok");
        firstResumed.get(3, TimeUnit.SECONDS);

        CompletableFuture<Void> third = queue.enqueue(() -> {
            events.add("third-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        assertFalse(third.isDone());

        releaseFirstResume.complete(null);
        third.get(3, TimeUnit.SECONDS);
        assertEquals(List.of(
            "first-start",
            "second-start",
            "first-resumed:ok",
            "third-start"), events);
        assertEquals(null, first.join());
    }

    @Test
    void yield_releasesGateAgainWhenResumedTurnSuspendsAgain() throws Exception {
        ZLinkAsyncSerialQueue queue = new ZLinkAsyncSerialQueue();
        CompletableFuture<String> firstIo = new CompletableFuture<>();
        CompletableFuture<String> secondIo = new CompletableFuture<>();
        CompletableFuture<Void> firstStarted = new CompletableFuture<>();
        CompletableFuture<Void> firstResumed = new CompletableFuture<>();
        List<String> events = new ArrayList<>();

        CompletableFuture<Void> first = queue.enqueue(() -> {
            events.add("first-start");
            firstStarted.complete(null);
            String firstResult = ZLinkYieldTurn.current().awaitFrameworkCallBlocking(firstIo);
            events.add("first-resumed:" + firstResult);
            firstResumed.complete(null);
            String secondResult = ZLinkYieldTurn.current().awaitFrameworkCallBlocking(secondIo);
            events.add("first-resumed-again:" + secondResult);
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();

        firstStarted.get(3, TimeUnit.SECONDS);

        CompletableFuture<Void> second = queue.enqueue(() -> {
            events.add("second-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        second.get(3, TimeUnit.SECONDS);

        firstIo.complete("one");
        firstResumed.get(3, TimeUnit.SECONDS);

        CompletableFuture<Void> third = queue.enqueue(() -> {
            events.add("third-start");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture();
        third.get(3, TimeUnit.SECONDS);

        secondIo.complete("two");
        first.get(3, TimeUnit.SECONDS);

        assertEquals(List.of(
            "first-start",
            "second-start",
            "first-resumed:one",
            "third-start",
            "first-resumed-again:two"), events);
    }
}
