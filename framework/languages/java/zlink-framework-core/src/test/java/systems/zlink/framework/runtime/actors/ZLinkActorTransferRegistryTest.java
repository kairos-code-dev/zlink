package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import org.junit.jupiter.api.Test;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorJoinCall;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.internal.handlers.ZLinkHandlerActivator;

final class ZLinkActorTransferRegistryTest {
    @Test
    void registeredAdapterTransfersDomainState() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("stateful", StatefulAdapter.class),
            ZLinkHandlerActivator.reflection());
        TestActor source = new TestActor("actor-1", null, "version-7");

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("stateful", source).toCompletableFuture().join();
        TestActor target = (TestActor) registry.transferIn(
            "stateful",
            "actor-1",
            null,
            state.state()).toCompletableFuture().join();

        assertEquals("stateful", state.adapterKey());
        assertEquals("version-7", target.state());
    }

    @Test
    void missingAdapterUsesEmptyStateAndFactorySignal() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of(),
            ZLinkHandlerActivator.reflection());

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("stateless", new TestActor("actor-1", null, "ignored"))
                .toCompletableFuture().join();

        assertNull(state.adapterKey());
        assertEquals(true, state.state().isEmpty());
        assertNull(registry.transferIn("stateless", "actor-1", null, state.state())
            .toCompletableFuture().join());
    }

    @Test
    void customAdapterMayReturnEmptyState() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("empty", EmptyAdapter.class),
            ZLinkHandlerActivator.reflection());

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("empty", new TestActor("actor-1", null, "ignored"))
                .toCompletableFuture().join();
        TestActor target = (TestActor) registry.transferIn(
            "empty", "actor-1", null, state.state()).toCompletableFuture().join();

        assertEquals("empty", state.adapterKey());
        assertEquals(true, state.state().isEmpty());
        assertEquals("loaded-elsewhere", target.state());
    }

    @Test
    void adapterCannotChangeActorIdentity() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("wrong", WrongIdAdapter.class),
            ZLinkHandlerActivator.reflection());

        CompletionException failure = assertThrows(
            CompletionException.class,
            () -> registry.transferIn("wrong", "actor-1", null, ZLinkMessage.empty())
                .toCompletableFuture().join());
        assertInstanceOf(ZLinkConfigurationException.class, failure.getCause());
    }

    public static final class StatefulAdapter implements ZLinkActorTransferAdapter<TestActor> {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(TestActor actor) {
            return CompletableFuture.completedFuture(ZLinkMessage.of(actor.state()));
        }

        @Override
        public java.util.concurrent.CompletionStage<TestActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            return CompletableFuture.completedFuture(new TestActor(
                actorId,
                context,
                state.decode(String.class)));
        }
    }

    public static class EmptyAdapter implements ZLinkActorTransferAdapter<TestActor> {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkMessage> transferOut(TestActor actor) {
            return CompletableFuture.completedFuture(ZLinkMessage.empty());
        }

        @Override
        public java.util.concurrent.CompletionStage<TestActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            return CompletableFuture.completedFuture(
                new TestActor(actorId, context, "loaded-elsewhere"));
        }
    }

    public static final class WrongIdAdapter extends EmptyAdapter {
        @Override
        public java.util.concurrent.CompletionStage<TestActor> transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state) {
            return CompletableFuture.completedFuture(
                new TestActor("different", context, "invalid"));
        }
    }

    record TestActor(String actorId, ZLinkActorContext suppliedContext, String state)
        implements ZLinkActor {
        @Override
        public ZLinkActorContext context() {
            return suppliedContext == null ? contextFor(actorId) : suppliedContext;
        }
    }

    private static ZLinkActorContext contextFor(String actorId) {
        return new ZLinkActorContext() {
            @Override public String actorId() { return actorId; }
            @Override public long objectGeneration() { return 1L; }
            @Override public String meshName() { return "test"; }
            @Override public java.util.Optional<String> spotId() {
                return java.util.Optional.empty();
            }
            @Override public systems.zlink.framework.actors.ZLinkBoundSession boundSession() {
                return null;
            }
            @Override public ZLinkActorJoinCall joinSpot(String spotId) {
                throw new UnsupportedOperationException();
            }
            @Override public ZLinkActorJoinCall joinSpot(String spotId, Object request) {
                throw new UnsupportedOperationException();
            }
            @Override public ZLinkActorJoinCall joinEntrySpot() {
                throw new UnsupportedOperationException();
            }
            @Override public ZLinkActorJoinCall joinEntrySpot(Object request) {
                throw new UnsupportedOperationException();
            }
        };
    }
}
