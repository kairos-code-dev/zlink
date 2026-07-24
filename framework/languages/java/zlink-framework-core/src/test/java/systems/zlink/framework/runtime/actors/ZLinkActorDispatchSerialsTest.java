package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;

final class ZLinkActorDispatchSerialsTest {
    @Test
    void queuedBarrierWaitsForActiveActorDispatch() {
        ZLinkActorDispatchSerials dispatches = new ZLinkActorDispatchSerials();
        CompletableFuture<Void> active = new CompletableFuture<>();
        List<String> order = new ArrayList<>();

        var first = dispatches.enqueue(
            dispatches.prepare("actor-1"),
            () -> {
                order.add("dispatch-started");
                return active.thenRun(() -> order.add("dispatch-completed"));
            });
        var barrier = dispatches.enqueue(
            dispatches.prepare("actor-1"),
            () -> {
                order.add("handoff-started");
                return CompletableFuture.completedFuture(null);
            });

        assertFalse(barrier.toCompletableFuture().isDone());
        active.complete(null);
        CompletableFuture.allOf(
            first.toCompletableFuture(), barrier.toCompletableFuture()).join();

        assertEquals(
            List.of("dispatch-started", "dispatch-completed", "handoff-started"),
            order);
    }

    @Test
    void allActorBarrierWaitsForEveryIndependentLane() {
        ZLinkActorDispatchSerials dispatches = new ZLinkActorDispatchSerials();
        CompletableFuture<Void> actorA = new CompletableFuture<>();
        CompletableFuture<Void> actorB = new CompletableFuture<>();

        dispatches.enqueue(
            dispatches.prepare("actor-a"),
            () -> actorA);
        dispatches.enqueue(
            dispatches.prepare("actor-b"),
            () -> actorB);

        CompletableFuture<Void> barrier =
            dispatches.awaitQuiescence().toCompletableFuture();
        assertFalse(barrier.isDone());
        actorA.complete(null);
        assertFalse(barrier.isDone());
        actorB.complete(null);
        barrier.join();
    }
}
