package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

/** Verifies peer admission, readiness, and lifecycle generation after restart. */
public final class MonA2LocationEventsScenario {
    private MonA2LocationEventsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        Contracts.RuntimePeer first = context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                snapshot -> snapshot.peers().stream().anyMatch(Contracts.RuntimePeer::ready),
                "MON-A2 initial peer was not ready")
            .peers().stream()
            .filter(Contracts.RuntimePeer::ready)
            .findFirst()
            .orElseThrow();
        int evidenceBaseline = context.evidenceEntryCount(context.serviceEndpoint());

        context.shutdownServiceB("MON-A2 service-b did not stop");
        context.awaitRuntimeSnapshot(
            context.serviceEndpoint(),
            snapshot -> snapshot.peers().stream().noneMatch(Contracts.RuntimePeer::ready),
            "MON-A2 stopped peer remained ready");

        context.restartServiceB();
        context.waitForPort(
            context.serviceBEndpoint(),
            true,
            "MON-A2 service-b did not restart");
        Contracts.RuntimePeer restarted = context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                snapshot -> snapshot.peers().stream().anyMatch(peer ->
                    peer.ready()
                        && peer.lifecycleGeneration() != first.lifecycleGeneration()),
                "MON-A2 restarted peer generation did not change")
            .peers().stream()
            .filter(peer ->
                peer.ready()
                    && peer.lifecycleGeneration() != first.lifecycleGeneration())
            .findFirst()
            .orElseThrow();

        MonitoringScenarioContext.ensure(
            restarted.rid().equals(first.rid()),
            "MON-A2 restarted peer RID changed");
        MonitoringScenarioContext.ensure(
            restarted.descriptorRevision() > 0
                && !restarted.endpoint().isBlank()
                && restarted.lastFailure() != null,
            "MON-A2 peer diagnostics are incomplete");
        context.waitForEvidenceAfter(
            context.serviceEndpoint(),
            evidenceBaseline,
            "route-mesh-runtime",
            "zlink.runtime.mesh_node.peer_changed");

        System.out.println("scenario MON-A2 passed");
    }
}
