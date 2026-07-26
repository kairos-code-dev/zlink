package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.*;

import java.lang.reflect.Proxy;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.*;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;

final class ZLinkActorRelocationStagingTest {
    @Test
    void preparedActorIsNotVisibleUntilAggregatePublication() {
        AtomicInteger destroys = new AtomicInteger();
        ZLinkActorRuntime runtime = runtime(destroys);

        var prepared = runtime.prepareRelocatedActor(
                "actor-a",
                "probe",
                new byte[0],
                false,
                null,
                () -> false,
                null)
            .toCompletableFuture().join();

        assertTrue(runtime.localActor("actor-a").isEmpty(),
            "factory completion must not expose partial target staging");
        assertSame(
            prepared.actor(),
            runtime.publishPreparedTransferredActor(prepared));
        assertSame(
            prepared.actor(),
            runtime.localActor("actor-a").orElseThrow());
        runtime.completePreparedTransferredActor(prepared);
        assertEquals(0, destroys.get());
    }

    @Test
    void discardedActorNeverEntersRegistryAndReleasesBackendResource() {
        AtomicInteger destroys = new AtomicInteger();
        ZLinkActorRuntime runtime = runtime(destroys);
        var prepared = runtime.prepareRelocatedActor(
                "actor-b",
                "probe",
                new byte[0],
                false,
                null,
                () -> false,
                null)
            .toCompletableFuture().join();

        runtime.discardPreparedTransferredActor(prepared)
            .toCompletableFuture().join();

        assertTrue(runtime.localActor("actor-b").isEmpty());
        assertEquals(1, destroys.get());
        assertThrows(
            IllegalStateException.class,
            () -> runtime.publishPreparedTransferredActor(prepared));
    }

    private static ZLinkActorRuntime runtime(AtomicInteger destroys) {
        ZLinkInternalSpotNode node = (ZLinkInternalSpotNode) Proxy.newProxyInstance(
            ZLinkInternalSpotNode.class.getClassLoader(),
            new Class<?>[] {ZLinkInternalSpotNode.class},
            (proxy, method, arguments) -> switch (method.getName()) {
                case "routingId" -> RoutingId.from("node-a");
                case "createActor" -> {
                    ((systems.zlink.contracts.messaging.Message) arguments[1]).close();
                    yield new ZLinkBackendActorRef(
                        RoutingId.from("node-a"),
                        (String) arguments[0],
                        7);
                }
                case "destroyActor" -> {
                    destroys.incrementAndGet();
                    yield CompletableFuture.completedFuture(null);
                }
                case "close" -> null;
                default -> defaultValue(method.getReturnType());
            });
        return new ZLinkActorRuntime(
            node,
            Map.of("probe", ProbeFactory.class),
            Duration.ofSeconds(5),
            new ZLinkJsonMessageSerializer());
    }

    private static Object defaultValue(Class<?> type) {
        if (!type.isPrimitive()) return null;
        if (type == boolean.class) return false;
        if (type == byte.class) return (byte) 0;
        if (type == short.class) return (short) 0;
        if (type == int.class) return 0;
        if (type == long.class) return 0L;
        if (type == float.class) return 0F;
        if (type == double.class) return 0D;
        if (type == char.class) return '\0';
        return null;
    }

    public static final class ProbeFactory implements ZLinkActorFactory {
        @Override
        public java.util.concurrent.CompletionStage<ZLinkActor> create(
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new ProbeActor(context));
        }
    }

    private record ProbeActor(ZLinkActorContext context) implements ZLinkActor {
    }
}
