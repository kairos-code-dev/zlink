package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.testkit.FakeZLinkBackendAdapterFactory;

final class BoundSessionTest {
    @Test
    void playActorPush_arrivesAtClientStream() {
        FakeZLinkBackendAdapterFactory backend = new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime runtime =
                 ZLinkFrameworkRuntime.start(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
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
                 ZLinkFrameworkRuntime.start(RemoteActorGatewayTest.options(), backend)) {
            ZLinkActor actor = runtime.actorManager()
                .create("player-1", "player")
                .toCompletableFuture()
                .join();
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
                 ZLinkFrameworkRuntime.start(RemoteActorGatewayTest.options(), backend)) {
            ZLinkSessionActor actor = runtime.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(new ZLinkActorRef(
                    RoutingId.from("play-node"),
                    "player-1",
                    1))
                .toCompletableFuture()
                .join();

            try (Message payload = Message.from("place")) {
                actor.relay(
                        new ZLinkStreamHeader("PlaceMark", Map.of(), Optional.empty()),
                        payload)
                    .toCompletableFuture()
                    .join();
            }
        }

        assertTrue(backend.calls().contains("stream.relayBoundActor.player-1.RAW.PlaceMark"));
    }
}
