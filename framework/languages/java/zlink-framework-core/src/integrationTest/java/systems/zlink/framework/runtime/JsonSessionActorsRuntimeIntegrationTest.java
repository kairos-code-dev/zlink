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
import systems.zlink.framework.messaging.ZLinkMessage;
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
                .create(actorId, "player")
                .toCompletableFuture()
                .join();
            ZLinkSessionActor bound = runtime.sessionActors(
                    "local-json",
                    RoutingId.from("json-session"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            bound.relay(
                    new ZLinkStreamHeader(
                        ZLinkStreamMessageKind.SEND,
                        ZLinkStreamCodec.JSON,
                        EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                        Optional.empty(),
                        "JsonRelaySend",
                        java.util.Map.of()),
                    ZLinkMessage.of(
                        new SessionActorsRuntimeIntegrationTest.JsonRelayReq("json-hello")))
                .toCompletableFuture()
                .join();

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
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.setRouterRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(SessionActorsRuntimeIntegrationTest.GameSpot.class);
                node.addEntrySpot(SessionActorsRuntimeIntegrationTest.GameEntrySpot.class); }; };
        options.addActorFactory(
            "player",
            SessionActorsRuntimeIntegrationTest.PlayerActorFactory.class);
        { var stream = options.addStreamNode("local-json"); stream.bind("inproc://local-json-bind-" + System.nanoTime());
            stream.registerSession(SessionActorsRuntimeIntegrationTest.GameSession.class); };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    public record JsonRelaySend(String value) {
    }

    public static final class DefaultJsonActorSendHandler {
        @ZLinkSpotActorSend
        public void handle(
            SessionActorsRuntimeIntegrationTest.PlayerActor actor,
            JsonRelaySend request) {
            SessionActorsRuntimeIntegrationTest.actorRelayRequests.offer(
                actor.actorId() + ":" + request.value());
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
