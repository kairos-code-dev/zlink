package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerFactory;

final class ZLinkActorTransferRegistryTest {
    private static final CancellationToken NONE = () -> false;

    @Test
    void registeredAdapterTransfersDomainState() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("stateful", StatefulAdapter.class),
            ZLinkHandlerFactory.reflection());
        TestActor source = new TestActor("actor-1", null, "version-7");

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("stateful", source, NONE);
        TestActor target = (TestActor) registry.transferIn(
            "stateful",
            "actor-1",
            null,
            state.state(),
            NONE);

        assertEquals("stateful", state.adapterKey());
        assertEquals("version-7", target.state());
    }

    @Test
    void missingAdapterUsesEmptyStateAndFactorySignal() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of(),
            ZLinkHandlerFactory.reflection());

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("stateless", new TestActor("actor-1", null, "ignored"), NONE);

        assertNull(state.adapterKey());
        assertEquals(true, state.state().isEmpty());
        assertNull(registry.transferIn("stateless", "actor-1", null, state.state(), NONE));
    }

    @Test
    void customAdapterMayReturnEmptyState() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("empty", EmptyAdapter.class),
            ZLinkHandlerFactory.reflection());

        ZLinkActorTransferRegistry.TransferState state =
            registry.transferOut("empty", new TestActor("actor-1", null, "ignored"), NONE);
        TestActor target = (TestActor) registry.transferIn(
            "empty", "actor-1", null, state.state(), NONE);

        assertEquals("empty", state.adapterKey());
        assertEquals(true, state.state().isEmpty());
        assertEquals("loaded-elsewhere", target.state());
    }

    @Test
    void adapterCannotChangeActorIdentity() {
        ZLinkActorTransferRegistry registry = new ZLinkActorTransferRegistry(
            java.util.Map.of("wrong", WrongIdAdapter.class),
            ZLinkHandlerFactory.reflection());

        assertThrows(
            ZLinkConfigurationException.class,
            () -> registry.transferIn("wrong", "actor-1", null, ZLinkMessage.empty(), NONE));
    }

    public static final class StatefulAdapter implements ZLinkActorTransferAdapter<TestActor> {
        @Override
        public ZLinkMessage transferOut(TestActor actor, CancellationToken cancellationToken) {
            return ZLinkMessage.of(actor.state());
        }

        @Override
        public TestActor transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken) {
            return new TestActor(
                actorId,
                context,
                state.decode(String.class));
        }
    }

    public static class EmptyAdapter implements ZLinkActorTransferAdapter<TestActor> {
        @Override
        public ZLinkMessage transferOut(TestActor actor, CancellationToken cancellationToken) {
            return ZLinkMessage.empty();
        }

        @Override
        public TestActor transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken) {
            return new TestActor(actorId, context, "loaded-elsewhere");
        }
    }

    public static final class WrongIdAdapter extends EmptyAdapter {
        @Override
        public TestActor transferIn(
            String actorId,
            ZLinkActorContext context,
            ZLinkMessage state,
            CancellationToken cancellationToken) {
            return new TestActor("different", context, "invalid");
        }
    }

    record TestActor(String actorId, ZLinkActorContext context, String state) implements ZLinkActor {
    }
}
