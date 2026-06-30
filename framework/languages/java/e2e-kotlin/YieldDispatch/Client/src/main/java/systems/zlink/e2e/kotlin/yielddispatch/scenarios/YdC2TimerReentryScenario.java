package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC2TimerReentryScenario {
    private YdC2TimerReentryScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String requestId = "YD-C2-" + UUID.randomUUID();
        String timerName = requestId + "-same";
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartCommand(
                    requestId,
                    timerName,
                    "yield-then-next",
                    50,
                    1500))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        waitForMarkers(connector, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"));
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStopCommand(requestId))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        System.out.println("scenario YD-C2 passed");
    }

    private static void waitForMarkers(
        ZLinkStreamConnector connector,
        String requestId,
        List<String> expected) {
        List<String> latest = List.of();
        for (int attempt = 0; attempt < 80; attempt++) {
            Contracts.EvidenceReply evidence = ClientStreamSupport.evidence(connector, requestId);
            latest = evidence.markers();
            if (startsWithMarkers(latest, expected)) {
                return;
            }
            ClientStreamSupport.sleep(100);
        }
        throw new IllegalStateException(
            "YD-C2 markers not observed in order: expected=" + expected + " actual=" + latest);
    }

    private static boolean startsWithMarkers(
        List<String> actual,
        List<String> expected) {
        if (actual.size() < expected.size()) {
            return false;
        }
        for (int i = 0; i < expected.size(); i++) {
            if (!actual.get(i).startsWith(expected.get(i) + "|")) {
                return false;
            }
        }
        return true;
    }
}
