package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonB2RegistrationValidationScenario {
    private MonB2RegistrationValidationScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String blockedSpot = "monitoring-local-blocked";
        context.post(context.serviceEndpoint(),
            "/runtime/multicast/create?rid=" + blockedSpot);
        context.post(context.serviceBEndpoint(), "/runtime/weight/zero");
        context.post(context.serviceEndpoint(),
            "/runtime/multicast/block?rid=" + blockedSpot);
        try {
            int localEvidenceStart =
                context.evidenceEntryCount(context.serviceEndpoint());
            context.publish(context.serviceEndpoint(), "b2-prime", 1);
            context.waitForEvidenceAfter(
                context.serviceEndpoint(),
                localEvidenceStart,
                "multicast",
                blockedSpot,
                "received");
            var result = context.publish(
                context.serviceEndpoint(), "b2-drop", 10000, true);
            MonitoringScenarioContext.ensure(result.snapshotLocal() >= 2,
                "MON-B2 publish did not snapshot both local targets: " + result);
            MonitoringScenarioContext.ensure(result.admittedLocal() >= 1,
                "MON-B2 available local target was not admitted: " + result);
            MonitoringScenarioContext.ensure(result.droppedLocal() >= 1,
                "MON-B2 blocked local target was not dropped: " + result);
            context.waitForEvent(
                context.serviceEndpoint(),
                "route-mesh-runtime",
                java.util.Set.of("zlink.runtime.mesh_node.multicast_dropped"));
            context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                value -> value.multicastDropped() > 0,
                "MON-B2 snapshot did not retain local target drop");
            MonitoringScenarioContext.ensure(
                context.evidenceCount(
                    context.serviceEndpoint(),
                    "multicast",
                    "monitoring-room-svc-a",
                    "received") > 0,
                "MON-B2 available local target did not receive payload");
            System.out.println("scenario MON-B2 passed");
        } finally {
            context.postBestEffort(context.serviceEndpoint(),
                "/runtime/multicast/release?rid=" + blockedSpot);
            context.postBestEffort(context.serviceBEndpoint(), "/runtime/weight/restore");
        }
    }
}
