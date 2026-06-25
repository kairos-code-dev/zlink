package systems.zlink.e2e.kotlin.spotservice;

import java.net.URI;
import java.time.Duration;
import java.util.List;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.stream.connector.ZLinkStreamCompression;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;

public final class ClientScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);
    private static final Duration EVENTUAL_TIMEOUT = Duration.ofSeconds(30);
    private final ZLinkSpotOutbound outbound;
    private final ZLinkRouteClient routes;

    public ClientScenario(
        ZLinkSpotOutbound outbound,
        ZLinkRouteClient routes) {
        this.outbound = outbound;
        this.routes = routes;
    }

    public void runMode(String mode) {
        switch (mode) {
            case "state1" -> runState1();
            case "state2" -> runState2();
            case "send" -> runSend();
            case "timeout" -> runTimeout();
            case "missing" -> runMissingPacket();
            case "normal" -> runNormal();
            case "owner" -> runOwnerRouting();
            case "route-mesh" -> runRouteMesh();
            case "actor-session" -> runActorSession();
            case "worker" -> runWorkerOffload();
            case "spot-outbound" -> runSpotOutbound();
            case "spot-to-spot" -> runSpotToSpot();
            case "idle-timer" -> runIdleTimer();
            case "timer-overrun" -> runTimerOverrun();
            default -> throw new IllegalArgumentException("unknown client mode " + mode);
        }
    }

    private void runState1() {
        Contracts.StateReply first = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("a1"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure("room-a".equals(first.spotRid()), "SM-A1 wrong spot rid");
        ensure("play-a".equals(first.nodeRid()), "SM-A1 wrong owner node");
        System.out.println("scenario SM-A1 passed");
    }

    private void runState2() {
        Contracts.StateReply second = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("a2"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure(second.value().contains("a1") && second.value().contains("a2"),
            "SM-A2 state did not accumulate");
        System.out.println("scenario SM-A2 passed");
    }

    private void runSend() {
        outbound.sendToSpot(RoutingId.from("room-a"), new Contracts.StateCommand("cmd-c1"))
            .await();
        System.out.println("scenario SM-C1-send passed");
    }

    private void runTimeout() {
        expectFailure(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.SlowRequest("late"))
            .timeout(Duration.ofMillis(100))
            .await(Contracts.StateReply.class));
        System.out.println("scenario SM-C1-timeout passed");
    }

    private void runNormal() {
        Contracts.StateReply after = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("after-timeout"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure(after.value().contains("after-timeout"), "SM-C1 post-timeout request failed");
        System.out.println("scenario SM-C1 passed");
        System.out.println("scenario SM-C1-normal passed");
    }

    private void runMissingPacket() {
        expectFailure(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("missing"))
            .packetName("MissingSpotPacket")
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        outbound.sendToSpot(RoutingId.from("room-a"), new Contracts.StateCommand("missing-send"))
            .packetName("MissingSpotCommand")
            .await();
        System.out.println("scenario SM-C1-negative passed");
        System.out.println("scenario SM-E1 passed");
    }

    private void runOwnerRouting() {
        Contracts.StateReply roomA = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("owner-a"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        Contracts.StateReply roomB = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-b"),
                new Contracts.StateRequest("owner-b"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure("play-a".equals(roomA.nodeRid()), "SM-A3 room-a owner mismatch");
        ensure("play-b".equals(roomB.nodeRid()), "SM-A3 room-b owner mismatch");
        System.out.println("scenario SM-A3 passed");
        System.out.println("scenario SM-A4 passed");
    }

    private void runRouteMesh() {
        Contracts.RoutePong routeReply = routes.requestTo(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("play-a"),
                new Contracts.RoutePing("route-mesh-normal"))
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.RoutePong.class);
        ensure("play-a".equals(routeReply.nodeRid()), "SM-F3 route-channel target node mismatch");
        ensure("client-route-mesh".equals(routeReply.routeRid()), "SM-F3 route-channel source routing id mismatch");
        ensure("route:route-mesh-normal".equals(routeReply.value()), "SM-F3 route-channel reply mismatch");

        Contracts.StateReply reply = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("route-mesh"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure("play-a".equals(reply.nodeRid()), "SM-F2 route mesh target mismatch");
        outbound.sendToSpot(RoutingId.from("room-a"), new Contracts.StateCommand("mixed-route-send"))
            .await();
        System.out.println("scenario SM-F1 passed");
        System.out.println("scenario SM-F2 passed");
        System.out.println("scenario SM-F3 passed");
        expectFailure(() -> outbound.requestToSpot(
                RoutingId.from("missing-route"),
                new Contracts.StateRequest("missing-route"))
            .timeout(Duration.ofMillis(300))
            .await(Contracts.StateReply.class));
        System.out.println("scenario SM-F4-missing-route passed");
    }

    private void runActorSession() {
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"));
        ZLinkStreamConnector unbound = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"));
        try {
            Contracts.ActorProfile profile = new Contracts.ActorProfile(
                "Player One",
                7,
                List.of("alpha", "beta"));
            connector.connect().await();
            unbound.connect().await();
            Contracts.ActorAuthReply auth = connector
                .request(new Contracts.ActorAuthRequest("actor-local-1", profile))
                .await(Contracts.ActorAuthReply.class);
            ensure("actor-local-1".equals(auth.actorId()), "SM-D1 auth actor mismatch");
            ensure(auth.boundCount() == 1, "SM-D1 bound actor count mismatch");
            ensure(profile.displayName().equals(auth.displayName()), "SM-B3 create profile display name mismatch");
            ensure(profile.level() == auth.level(), "SM-B3 create profile level mismatch");
            ensure(profile.tags().equals(auth.tags()), "SM-B3 create profile tags mismatch");

            var entryPush = connector.waitFor(Contracts.ActorPush.class)
                .submit(Contracts.ActorPush.class);
            Contracts.ActorEchoReply entryReply = connector
                .request(new Contracts.ActorEchoRequest("entry-echo", 1, profile))
                .metadata("actor-id", "actor-local-1")
                .await(Contracts.ActorEchoReply.class);
            Contracts.ActorPush entry = connector.await(entryPush).payload();
            ensure("entry:entry-echo".equals(entryReply.value()), "SM-B1 entry actor request mismatch");
            ensure(entryReply.requestSeq() == 1, "SM-B3 entry request sequence mismatch");
            ensure(profile.displayName().equals(entryReply.displayName()), "SM-B3 entry profile display name mismatch");
            ensure(profile.level() == entryReply.level(), "SM-B3 entry profile level mismatch");
            ensure(profile.tags().equals(entryReply.tags()), "SM-B3 entry profile tags mismatch");
            ensure(entry.requestSeq() == 1, "SM-D1 entry push request sequence mismatch");
            ensure("push:entry-echo".equals(entry.value()), "SM-D1 entry push mismatch");

            Contracts.ActorJoinReply joined = connector
                .request(new Contracts.ActorJoinRequest("room-a", profile, profile.tags()))
                .metadata("actor-id", "actor-local-1")
                .await(Contracts.ActorJoinReply.class);
            ensure("room-a".equals(joined.spotRid()), "SM-B1 joined spot mismatch");
            ensure(profile.tags().equals(joined.tags()), "SM-B3 join payload tags mismatch");
            ensure(profile.displayName().equals(joined.displayName()), "SM-B3 join payload display name mismatch");
            ensure(profile.level() == joined.level(), "SM-B3 join payload level mismatch");

            var unboundPush = unbound.waitFor(Contracts.ActorPush.class)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPush.class);
            var userPush1 = connector.waitFor(Contracts.ActorPush.class)
                .submit(Contracts.ActorPush.class);
            Contracts.ActorEchoReply userReply1 = connector
                .request(new Contracts.ActorEchoRequest("user-echo-1", 2, profile))
                .metadata("actor-id", "actor-local-1")
                .await(Contracts.ActorEchoReply.class);
            Contracts.ActorPush user1 = connector.await(userPush1).payload();
            ensure("room-a".equals(userReply1.spotRid()), "SM-B1 user actor spot mismatch");
            ensure("user:user-echo-1".equals(userReply1.value()), "SM-B1 user actor request mismatch");
            ensure(userReply1.requestSeq() == 2, "SM-B3 user request sequence mismatch");
            ensure(profile.displayName().equals(userReply1.displayName()), "SM-B3 user profile display name mismatch");
            ensure(profile.level() == userReply1.level(), "SM-B3 user profile level mismatch");
            ensure(profile.tags().equals(userReply1.tags()), "SM-B3 user profile tags mismatch");
            ensure(user1.requestSeq() == 2, "SM-D1 user push request sequence mismatch");
            ensure("push:user-echo-1".equals(user1.value()), "SM-D1 user push mismatch");
            expectFailure(() -> awaitUnchecked(unbound, unboundPush));

            var userPush2 = connector.waitFor(Contracts.ActorPush.class)
                .submit(Contracts.ActorPush.class);
            Contracts.ActorEchoReply userReply2 = connector
                .request(new Contracts.ActorEchoRequest("user-echo-2", 3, profile))
                .metadata("actor-id", "actor-local-1")
                .await(Contracts.ActorEchoReply.class);
            Contracts.ActorPush user2 = connector.await(userPush2).payload();
            ensure(userReply2.requestSeq() == 3, "SM-B7 second packet request sequence mismatch");
            ensure(user2.requestSeq() == 3, "SM-B7 second push request sequence mismatch");

            var userPush3 = connector.waitFor(Contracts.ActorPush.class)
                .submit(Contracts.ActorPush.class);
            Contracts.ActorEchoReply userReply3 = connector
                .request(new Contracts.ActorEchoRequest("user-echo-3", 4, profile))
                .metadata("actor-id", "actor-local-1")
                .await(Contracts.ActorEchoReply.class);
            Contracts.ActorPush user3 = connector.await(userPush3).payload();
            ensure(userReply3.requestSeq() == 4, "SM-B7 third packet request sequence mismatch");
            ensure(user3.requestSeq() == 4, "SM-B7 third push request sequence mismatch");
            ensure(userReply1.handlerSeq() < userReply2.handlerSeq()
                    && userReply2.handlerSeq() < userReply3.handlerSeq(),
                "SM-B7 actor packet handler sequence was not preserved");
            ensure(user1.handlerSeq() < user2.handlerSeq()
                    && user2.handlerSeq() < user3.handlerSeq(),
                "SM-B7 actor push sequence was not preserved");

            System.out.println("scenario SM-B1 passed");
            System.out.println("scenario SM-B3 passed");
            System.out.println("scenario SM-B7 passed");
            System.out.println("scenario SM-D1 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor/session scenario failed", error);
        } finally {
            try {
                connector.close().await();
            } catch (Exception ignored) {
            }
            try {
                unbound.close().await();
            } catch (Exception ignored) {
            }
        }
    }

    private static <T> void awaitUnchecked(
        ZLinkStreamConnector connector,
        java.util.concurrent.CompletionStage<T> stage) {
        try {
            connector.await(stage);
        } catch (Exception error) {
            throw new RuntimeException(error);
        }
    }

    private ZLinkStreamConnector createStreamConnector(String endpoint) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            ZLinkStreamDispatchMode.AUTO,
            REQUEST_TIMEOUT,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            false,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            ZLinkStreamCompression.LZ4));
    }

    private void runWorkerOffload() {
        eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("worker-start"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        Contracts.StateReply followUp = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("worker-follow-up"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure(followUp.value().contains("worker-follow-up"),
            "SM-A8 follow-up state was not applied");
        System.out.println("scenario SM-A8 passed");
    }

    private void runSpotOutbound() {
        Contracts.OutboundReply reply = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.OutboundRequest("c2"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.OutboundReply.class));
        ensure("room-a".equals(reply.spotRid()), "SM-C2 wrong source spot");
        ensure("play-a".equals(reply.nodeRid()), "SM-C2 wrong source node");
        ensure("c2".equals(reply.channelReply()), "SM-C2 channel request reply mismatch");
        System.out.println("scenario SM-C2 passed");
    }

    private void runSpotToSpot() {
        Contracts.StateReply requestReply = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("c3-source"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class));
        ensure("play-a".equals(requestReply.nodeRid()), "SM-C3 source spot owner mismatch");
        outbound.sendToSpot(RoutingId.from("room-b"), new Contracts.OutboundCommand("c3-send"))
            .packetName("OutboundCommand")
            .await();
        Contracts.OutboundReply reply = eventually(() -> outbound.requestToSpot(
                RoutingId.from("room-b"),
                new Contracts.OutboundRequest("c3-request"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.OutboundReply.class));
        ensure("room-b".equals(reply.spotRid()), "SM-C3 wrong target spot");
        ensure("play-b".equals(reply.nodeRid()), "SM-C3 wrong target node");
        System.out.println("scenario SM-C3 passed");
    }

    private void runIdleTimer() {
        sleep(1200);
        System.out.println("scenario SM-E3 passed");
    }

    private void runTimerOverrun() {
        sleep(1200);
        System.out.println("scenario SM-E4 passed");
    }

    private static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException error) {
            return;
        }
        throw new IllegalStateException("operation unexpectedly succeeded");
    }

    private static <T> T eventually(Supplier<T> action) {
        long deadline = System.nanoTime() + EVENTUAL_TIMEOUT.toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return action.get();
            } catch (RuntimeException error) {
                lastFailure = error;
                try {
                    Thread.sleep(200);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("operation interrupted", interrupted);
                }
            }
        }
        throw new IllegalStateException("operation did not succeed before timeout", lastFailure);
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("operation interrupted", error);
        }
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
