package systems.zlink.e2e.spotservice;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotOutbound;

public final class ClientScenario {
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
            default -> throw new IllegalArgumentException("unknown client mode " + mode);
        }
    }

    private void runState1() {
        Contracts.StateReply first = outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("a1"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.StateReply.class);
        ensure("room-a".equals(first.spotRid()), "SM-A1 wrong spot rid");
        ensure("play-a".equals(first.nodeRid()), "SM-A1 wrong owner node");
        System.out.println("scenario SM-A1 passed");
    }

    private void runState2() {
        Contracts.StateReply second = outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("a2"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.StateReply.class);
        ensure(second.value().endsWith("a1,a2"), "SM-A2 state did not accumulate");
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
        Contracts.StateReply after = outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("after-timeout"))
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.StateReply.class);
        ensure(after.value().contains("after-timeout"), "SM-C1 post-timeout request failed");
        System.out.println("scenario SM-C1-normal passed");
    }

    private void runMissingPacket() {
        expectFailure(() -> outbound.requestToSpot(
                RoutingId.from("room-a"),
                new Contracts.StateRequest("missing"))
            .packetName("MissingSpotPacket")
            .timeout(Duration.ofSeconds(3))
            .await(Contracts.StateReply.class));
        outbound.sendToSpot(RoutingId.from("room-a"), new Contracts.StateCommand("missing-send"))
            .packetName("MissingSpotCommand")
            .await();
        System.out.println("scenario SM-C1-negative passed");
    }

    private static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException error) {
            return;
        }
        throw new IllegalStateException("operation unexpectedly succeeded");
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
