package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonB1KindFilterScenario {
    private MonB1KindFilterScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String remoteSpot = "monitoring-room-svc-b";
        context.post(context.serviceBEndpoint(),
            "/runtime/multicast/block?rid=" + remoteSpot);
        try {
            int remoteEvidenceStart =
                context.evidenceEntryCount(context.serviceBEndpoint());
            context.publish(context.serviceEndpoint(), "b1-prime", 1);
            context.waitForEvidenceAfter(
                context.serviceBEndpoint(),
                remoteEvidenceStart,
                "multicast",
                remoteSpot,
                "received");
            var result = context.publish(
                context.serviceEndpoint(), "b1-pressure", 10000);
            MonitoringScenarioContext.ensure(
                result.status().equals("BACKPRESSURED")
                    || result.status().equals("TIMED_OUT")
                    || result.droppedRemote() > 0,
                "MON-B1 publish did not expose remote pressure: " + result);
            MonitoringScenarioContext.ensure(result.snapshotRemote() >= 1,
                "MON-B1 publish did not snapshot a remote target: " + result);
            context.waitForEvent(
                context.serviceEndpoint(),
                "route-mesh-runtime",
                java.util.Set.of("zlink.runtime.mesh_node.multicast_backpressured"));
            var sourceSnapshot = context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                value -> value.multicastSubmitted() > 0
                    && value.multicastBackpressured() > 0,
                "MON-B1 source snapshot did not retain multicast pressure");
            MonitoringScenarioContext.ensure(sourceSnapshot.multicastSubmitted() > 0,
                "MON-B1 snapshot did not count submitted multicast");
            MonitoringScenarioContext.ensure(
                context.evidenceCount(
                    context.serviceEndpoint(),
                    "multicast",
                    "monitoring-room-svc-a",
                    "received") > 0,
                "MON-B1 accepted local target did not receive payload");
            System.out.println("scenario MON-B1 passed");
        } finally {
            context.postBestEffort(context.serviceBEndpoint(),
                "/runtime/multicast/release?rid=" + remoteSpot);
        }
    }
}
