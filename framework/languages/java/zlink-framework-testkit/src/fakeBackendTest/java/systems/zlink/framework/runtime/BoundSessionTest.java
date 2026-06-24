package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class BoundSessionTest {
    @Test
    void playActorPush_arrivesAtClientStream() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            actor.context()
                .boundSession()
                .send("push")
                .packetName("Push")
                .submit()
                .toCompletableFuture()
                .join();
        }

        assertTrue(backend.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.session-1.Push.")
                && call.endsWith(".push")));
    }

    @Test
    void boundSessionDisconnect_unbindsCurrentActorSession() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            actor.context()
                .boundSession()
                .disconnect()
                .toCompletableFuture()
                .join();

            org.junit.jupiter.api.Assertions.assertThrows(
                ZLinkConfigurationException.class,
                () -> actor.context().boundSession());
        }

        assertTrue(backend.calls().contains("stream.unbindActor.player-1"));
    }

    @Test
    void sessionActorRelay_sendsPacketThroughBoundActorBackendRoute() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 RuntimeTestSupport.startFramework(RemoteActorGatewayTest.options(), backend)) {
            ZLinkSessionActor actor = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(new ZLinkActorRef(
                    RoutingId.from("play-node"),
                    "player-1",
                    1))
                .toCompletableFuture()
                .join();

            relayWithHeader(actor, "PlaceMark", ZLinkMessage.of("place"));
        }

        assertTrue(backend.calls().contains("stream.relayBoundActor.player-1.RAW.PlaceMark"));
    }

    private static ZLinkActor managedActor(
        ZLinkFrameworkRuntime runtime,
        String actorId,
        String actorType) {
        return ((ZLinkActorRuntime) runtime.actorManager())
            .getOrCreateManagedActor(actorId, actorType)
            .toCompletableFuture()
            .join();
    }

    private static void relayWithHeader(
        ZLinkSessionActor actor,
        String packetName,
        ZLinkMessage payload) {
        ZLinkSessionActorsRuntime.enterRelayDispatch(
            new ZLinkStreamHeader(packetName, Map.of(), Optional.empty()));
        try {
            actor.relay(payload).toCompletableFuture().join();
        } finally {
            ZLinkSessionActorsRuntime.exitRelayDispatch();
        }
    }
}
