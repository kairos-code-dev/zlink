package systems.zlink.framework.runtime;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.EnumSet;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class JsonSessionActorsRuntimeIntegrationTest {
    @Test
    void sessionGateway_relaysJsonActorSendWithDefaultPacketName() throws Exception {
        SessionActorsRuntimeIntegrationTest.actorRelayRequests.clear();
        Zlink.version();
        String actorId =
            SessionActorsRuntimeIntegrationTest.uniqueActorId("json-player");
        try (ZLinkFrameworkRuntime runtime = startLocalJsonRuntime()) {
            var actor = runtime.actorManager()
                .createAsync(actorId, "player")
                .toCompletableFuture()
                .join();
            ZLinkSessionActor bound = runtime.sessionActors(
                    "local-json",
                    RoutingId.from("json-session"))
                .bindAsync(actor)
                .toCompletableFuture()
                .join();

            try (Message payload = Message.from("{\"value\":\"json-hello\"}")) {
                bound.relayAsync(
                        new ZLinkStreamHeader(
                            ZLinkStreamMessageKind.SEND,
                            ZLinkStreamCodec.JSON,
                            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                            Optional.empty(),
                            "JsonRelaySend",
                            java.util.Map.of()),
                        payload)
                    .toCompletableFuture()
                    .join();
            }

            assertEquals(
                actorId + ":json-hello",
                awaitActorRelay(
                    actorId + ":json-hello",
                    2,
                    TimeUnit.SECONDS));
        }
    }

    private static ZLinkFrameworkRuntime startLocalJsonRuntime() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().addJson();
        options.addHandlersFromPackageOf(JsonSessionActorsRuntimeIntegrationTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.setRoutingId(RoutingId.from("play-node")));
                node.addSpotFactory(SessionActorsRuntimeIntegrationTest.GameSpot.class);
                node.addEntrySpot(SessionActorsRuntimeIntegrationTest.GameEntrySpot.class);
            }));
        options.addActorFactory(
            "player",
            SessionActorsRuntimeIntegrationTest.PlayerActorFactory.class);
        options.addStreamNode("local-json", stream -> {
            stream.bind("inproc://local-json-bind-" + System.nanoTime());
            stream.registerSession(SessionActorsRuntimeIntegrationTest.GameSession.class);
        });

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    public record JsonRelaySend(String value) {
    }

    public static final class DefaultJsonActorSendHandler {
        @ZLinkSpotActorSend
        public CompletionStage<Void> handleAsync(
            SessionActorsRuntimeIntegrationTest.PlayerActor actor,
            JsonRelaySend request) {
            SessionActorsRuntimeIntegrationTest.actorRelayRequests.offer(
                actor.actorId() + ":" + request.value());
            return CompletableFuture.completedFuture(null);
        }
    }

    private static String awaitActorRelay(
        String expected,
        long timeout,
        TimeUnit unit) throws Exception {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (true) {
            long remaining = deadline - System.nanoTime();
            if (remaining <= 0) {
                throw new java.util.concurrent.TimeoutException();
            }
            String received = SessionActorsRuntimeIntegrationTest.actorRelayRequests.poll(
                remaining,
                TimeUnit.NANOSECONDS);
            if (expected.equals(received)) {
                return received;
            }
        }
    }
}
