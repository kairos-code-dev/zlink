package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;

final class ZLinkDeferredActorJoinScopeTest {
    @Test
    void activatesOnlyAfterNormalHandlerTerminal() {
        ZLinkActorDispatchSerials serials = new ZLinkActorDispatchSerials();
        List<String> order = new ArrayList<>();

        serials.runTurn("actor-a", () -> {
            order.add("handler");
            ZLinkDeferredActorJoinScope.register(
                "actor-a",
                4,
                Long.MAX_VALUE,
                () -> {
                    order.add("join");
                    return CompletableFuture.completedFuture(null);
                });
            order.add("terminal");
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture().join();

        assertEquals(List.of("handler", "terminal", "join"), order);
    }

    @Test
    void awaitedJavaContinuationUsesTheOpenActorScope() {
        ZLinkActorDispatchSerials serials = new ZLinkActorDispatchSerials();
        CompletableFuture<Void> awaited = new CompletableFuture<>();
        List<String> order = new ArrayList<>();

        CompletableFuture<Void> turn = serials.runTurn(
            "actor-a",
            () -> awaited.thenRunAsync(() -> {
                order.add("continuation");
                ZLinkDeferredActorJoinScope.register(
                    "actor-a",
                    0,
                    Long.MAX_VALUE,
                    () -> {
                        order.add("join");
                        return CompletableFuture.completedFuture(null);
                    });
            })).toCompletableFuture();

        assertTrue(order.isEmpty());
        awaited.complete(null);
        turn.join();
        assertEquals(List.of("continuation", "join"), order);
    }

    @Test
    void discardsEveryInactiveIntentWhenHandlerFails() {
        ZLinkActorDispatchSerials serials = new ZLinkActorDispatchSerials();
        List<String> order = new ArrayList<>();

        CompletionException failure = assertThrows(
            CompletionException.class,
            () -> serials.runTurn("actor-a", () -> {
                ZLinkDeferredActorJoinScope.register(
                    "actor-a",
                    4,
                    Long.MAX_VALUE,
                    () -> {
                        order.add("join");
                        return CompletableFuture.completedFuture(null);
                    });
                return CompletableFuture.failedFuture(
                    new IllegalStateException("handler failed"));
            }).toCompletableFuture().join());

        assertTrue(failure.getCause() instanceof IllegalStateException);
        assertTrue(order.isEmpty());

        serials.runTurn("actor-a", () -> {
            ZLinkDeferredActorJoinScope.register(
                "actor-a",
                0,
                Long.MAX_VALUE,
                () -> CompletableFuture.completedFuture(null));
            return CompletableFuture.completedFuture(null);
        }).toCompletableFuture().join();
    }

    @Test
    void rejectsDetachedWrongActorOversizedAndDuplicateClaims() {
        ZLinkFrameworkException detached = assertThrows(
            ZLinkFrameworkException.class,
            () -> ZLinkDeferredActorJoinScope.register(
                "actor-a",
                0,
                Long.MAX_VALUE,
                () -> CompletableFuture.completedFuture(null)));
        assertEquals(
            ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
            detached.kind());

        try (ZLinkDeferredActorJoinScope.Scope scope =
                 ZLinkDeferredActorJoinScope.enter("actor-a")) {
            ZLinkFrameworkException wrongActor = assertThrows(
                ZLinkFrameworkException.class,
                () -> ZLinkDeferredActorJoinScope.register(
                    "actor-b",
                    0,
                    Long.MAX_VALUE,
                    () -> CompletableFuture.completedFuture(null)));
            assertEquals(
                ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                wrongActor.kind());

            ZLinkFrameworkException oversized = assertThrows(
                ZLinkFrameworkException.class,
                () -> ZLinkDeferredActorJoinScope.register(
                    "actor-a",
                    ZLinkDeferredActorJoinScope.MAX_REQUEST_BYTES + 1,
                    Long.MAX_VALUE,
                    () -> CompletableFuture.completedFuture(null)));
            assertEquals(
                ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                oversized.kind());

            ZLinkDeferredActorJoinScope.register(
                "actor-a",
                0,
                Long.MAX_VALUE,
                () -> CompletableFuture.completedFuture(null));
            ZLinkFrameworkException moving = assertThrows(
                ZLinkFrameworkException.class,
                () -> ZLinkDeferredActorJoinScope.register(
                    "actor-a",
                    0,
                    Long.MAX_VALUE,
                    () -> CompletableFuture.completedFuture(null)));
            assertEquals(ZLinkFrameworkErrorKind.ACTOR_MOVING, moving.kind());
            scope.finish(
                CompletableFuture.completedFuture(null),
                null).toCompletableFuture().join();
        }
    }

    @Test
    void userOrEntryHandlerRegistersSeveralMemberActorsInCallOrder() {
        List<String> order = new ArrayList<>();

        ZLinkDeferredActorJoinHandlerScope.run(
            actorId -> actorId.equals("actor-a") || actorId.equals("actor-b"),
            () -> {
                ZLinkDeferredActorJoinScope.register(
                    "actor-a",
                    2,
                    Long.MAX_VALUE,
                    () -> {
                        order.add("actor-a");
                        return CompletableFuture.completedFuture(null);
                    });
                ZLinkDeferredActorJoinScope.register(
                    "actor-b",
                    2,
                    Long.MAX_VALUE,
                    () -> {
                        order.add("actor-b");
                        return CompletableFuture.completedFuture(null);
                    });
                order.add("handler-terminal");
                return CompletableFuture.completedFuture(null);
            }).toCompletableFuture().join();

        assertEquals(
            List.of("handler-terminal", "actor-a", "actor-b"),
            order);
    }

    @Test
    void userOrEntryHandlerRejectsAnActorOutsideItsMembershipProjection() {
        CompletionException failure = assertThrows(
            CompletionException.class,
            () -> ZLinkDeferredActorJoinHandlerScope.run(
                actorId -> actorId.equals("actor-a"),
                () -> {
                    ZLinkDeferredActorJoinScope.register(
                        "actor-b",
                        0,
                        Long.MAX_VALUE,
                        () -> CompletableFuture.completedFuture(null));
                    return CompletableFuture.completedFuture(null);
                }).toCompletableFuture().join());

        assertTrue(failure.getCause() instanceof ZLinkFrameworkException);
        assertEquals(
            ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
            ((ZLinkFrameworkException) failure.getCause()).kind());
    }

    @Test
    void spotHandlerReservesEachActorMailboxBarrierBeforeItsTerminal() {
        List<String> order = new ArrayList<>();
        java.util.concurrent.atomic.AtomicReference<
            java.util.function.Supplier<CompletionStage<Void>>> queued =
            new java.util.concurrent.atomic.AtomicReference<>();

        CompletionStage<Void> handler = ZLinkDeferredActorJoinHandlerScope.run(
            actorId -> actorId.equals("actor-a"),
            () -> {
                ZLinkDeferredActorJoinScope.registerWithActorBarrier(
                    "actor-a",
                    0,
                    Long.MAX_VALUE,
                    () -> {
                        order.add("join");
                        return CompletableFuture.completedFuture(null);
                    },
                    operation -> {
                        order.add("barrier-reserved");
                        queued.set(operation);
                        return CompletableFuture.completedFuture(null)
                            .thenCompose(ignored -> operation.get());
                    });
                order.add("handler-terminal");
                return CompletableFuture.completedFuture(null);
            });

        handler.toCompletableFuture().join();
        assertTrue(queued.get() != null);
        assertEquals(
            List.of("barrier-reserved", "handler-terminal", "join"),
            order);
    }
}
