package systems.zlink.framework.execution;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.runtime.internal.relocation
    .ZLinkCompositeRelocationBarrier;

final class ZLinkCompositeRelocationBarrierTest {
    @Test
    void sealsSpotActorAndTimerAsOneGeneration() throws Exception {
        ZLinkAsyncSerialQueue spot = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue actor = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue timer = new ZLinkAsyncSerialQueue();
        ZLinkCompositeRelocationBarrier barrier =
            new ZLinkCompositeRelocationBarrier();

        var seal = barrier.trySeal(lanes(spot, actor, timer))
            .orElseThrow();
        assertEquals(1L, seal.generation());
        assertEquals(
            List.of("spot", "actor:a", "timer:t"),
            seal.laneIds());

        CompletableFuture<Void> spotHeld = spot.enqueueRelocatable(
            new byte[] {1},
            () -> CompletableFuture.failedFuture(
                new AssertionError("held Spot ingress ran")))
            .toCompletableFuture();
        CompletableFuture<Void> actorHeld = actor.enqueueRelocatable(
            new byte[] {2},
            () -> CompletableFuture.failedFuture(
                new AssertionError("held Actor ingress ran")))
            .toCompletableFuture();
        CompletableFuture<Void> timerHeld = timer.enqueueRelocatable(
            new byte[] {3},
            () -> CompletableFuture.failedFuture(
                new AssertionError("held timer ingress ran")))
            .toCompletableFuture();

        assertEquals(
            "captured",
            barrier.runCapture(
                seal,
                () -> CompletableFuture.completedFuture("captured"))
                .toCompletableFuture()
                .get(3, TimeUnit.SECONDS));

        var committed = barrier.commit(seal).orElseThrow();
        CompletableFuture.allOf(
            spotHeld, actorHeld, timerHeld).get(3, TimeUnit.SECONDS);
        assertEquals(1, committed.get("spot").size());
        assertEquals(1, committed.get("actor:a").size());
        assertEquals(1, committed.get("timer:t").size());
        assertTrue(barrier.commit(seal).isEmpty());
    }

    @Test
    void failedLaneRollsBackEveryEarlierSeal() throws Exception {
        ZLinkAsyncSerialQueue spot = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue actor = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue timer = new ZLinkAsyncSerialQueue();
        ZLinkCompositeRelocationBarrier barrier =
            new ZLinkCompositeRelocationBarrier();
        CompletableFuture<Void> actorActive = new CompletableFuture<>();
        CompletableFuture<Void> actorStarted = new CompletableFuture<>();

        actor.enqueue(() -> {
            actorStarted.complete(null);
            return actorActive;
        });
        actorStarted.get(3, TimeUnit.SECONDS);

        assertTrue(barrier.trySeal(lanes(spot, actor, timer)).isEmpty());
        CompletableFuture<Void> spotIngress = spot.enqueue(
            () -> CompletableFuture.completedFuture(null))
            .toCompletableFuture();
        spotIngress.get(3, TimeUnit.SECONDS);

        actorActive.complete(null);
        actor.awaitQuiescence().toCompletableFuture()
            .get(3, TimeUnit.SECONDS);
        var seal = barrier.trySeal(lanes(spot, actor, timer))
            .orElseThrow();
        assertTrue(barrier.abort(seal));
        assertFalse(barrier.abort(seal));
    }

    @Test
    void yieldedActorContinuationPreventsPartialAggregateSeal()
        throws Exception {
        ZLinkAsyncSerialQueue spot = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue actor = new ZLinkAsyncSerialQueue();
        ZLinkAsyncSerialQueue timer = new ZLinkAsyncSerialQueue();
        ZLinkCompositeRelocationBarrier barrier =
            new ZLinkCompositeRelocationBarrier();
        CompletableFuture<Void> remote = new CompletableFuture<>();
        CompletableFuture<Void> yielded = new CompletableFuture<>();

        CompletableFuture<Void> dispatch = actor.enqueue(() -> {
            var continuation =
                ZLinkAsyncSerialQueue.yieldCurrent(remote);
            yielded.complete(null);
            return continuation;
        }).toCompletableFuture();
        yielded.get(3, TimeUnit.SECONDS);

        assertTrue(barrier.trySeal(lanes(spot, actor, timer)).isEmpty());
        remote.complete(null);
        dispatch.get(3, TimeUnit.SECONDS);

        var seal = barrier.trySeal(lanes(spot, actor, timer))
            .orElseThrow();
        assertThrows(
            IllegalStateException.class,
            () -> barrier.runCapture(
                null,
                () -> CompletableFuture.completedFuture(null)));
        assertTrue(barrier.abort(seal));
    }

    private static LinkedHashMap<String, ZLinkAsyncSerialQueue> lanes(
        ZLinkAsyncSerialQueue spot,
        ZLinkAsyncSerialQueue actor,
        ZLinkAsyncSerialQueue timer) {
        LinkedHashMap<String, ZLinkAsyncSerialQueue> lanes =
            new LinkedHashMap<>();
        lanes.put("spot", spot);
        lanes.put("actor:a", actor);
        lanes.put("timer:t", timer);
        return lanes;
    }
}
