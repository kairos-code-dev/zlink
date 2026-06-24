package systems.zlink.e2e.spotservice;

import java.time.Duration;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

public final class ClientScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);
    private static final Duration EVENTUAL_TIMEOUT = Duration.ofSeconds(30);
    private final ZLinkSpotOutbound outbound;

    public ClientScenario(ZLinkSpotOutbound outbound) {
        this.outbound = outbound;
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
        Contracts.StateReply reply = outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("route-mesh"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class);
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
        System.out.println("scenario SM-F4 passed");
        Contracts.StateReply after = outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("after-route-negative"))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.StateReply.class);
        ensure(after.value().contains("after-route-negative"), "SM-F5 route lifecycle polluted channel");
        System.out.println("scenario SM-F5 passed");
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

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
