package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdE1TimeoutScenario {
    private YdE1TimeoutScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String requestId = "YD-E1-" + UUID.randomUUID();
        ClientStreamSupport.send(
            connector.send(new Contracts.YieldTimeoutCommand(requestId, 800, 100))
                .packetName("YieldTimeoutCommand")
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        waitForMarkers(connector, requestId, List.of(
            "timeout-yield-started",
            "timeout-yield-released",
            "timeout-yield-completed"));
        ClientStreamSupport.send(
            connector.send(new Contracts.SpotProbeCommand(requestId, "timeout-probe"))
                .packetName("SpotProbeCommand")
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, "room-a"));
        waitForMarkers(connector, requestId, List.of(
            "timeout-yield-started",
            "timeout-yield-released",
            "timeout-yield-completed",
            "probe-completed"));
        System.out.println("scenario YD-E1 passed");
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
            "YD-E1 markers not observed in order: expected=" + expected + " actual=" + latest);
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
