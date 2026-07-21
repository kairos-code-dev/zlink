package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

/** Verifies the exact RouteMesh runtime snapshot as one immutable value. */
public final class MonA1SocketEventsScenario {
    private MonA1SocketEventsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        Contracts.RuntimeSnapshot baseline =
            context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                snapshot -> snapshot.peers().stream().noneMatch(Contracts.RuntimePeer::ready),
                "MON-A1 baseline retained a ready peer");
        int evidenceBaseline = context.evidenceEntryCount(context.serviceEndpoint());
        context.restartServiceB();
        context.waitForPort(
            context.serviceBEndpoint(),
            true,
            "MON-A1 service-b did not restart");
        Contracts.RuntimeSnapshot current = context.awaitRuntimeSnapshot(
            context.serviceEndpoint(),
            snapshot -> snapshot.peers().stream().anyMatch(peer ->
                peer.ready()
                    && peer.channelNames().contains(Contracts.SPOT_CHANNEL)),
            "MON-A1 ready peer was not reflected in the current snapshot");

        MonitoringScenarioContext.ensure(
            Contracts.SPOT_MESH.equals(baseline.meshName()),
            "MON-A1 snapshot MeshName mismatch");
        MonitoringScenarioContext.ensure(
            !baseline.rid().isBlank()
                && baseline.lifecycleGeneration() > 0
                && baseline.descriptorRevision() > 0
                && !baseline.endpoint().isBlank(),
            "MON-A1 snapshot identity fields are incomplete");
        MonitoringScenarioContext.ensure(
            current.sequence() > baseline.sequence(),
            "MON-A1 snapshot sequence did not advance");
        MonitoringScenarioContext.ensure(
            baseline.channels().stream().anyMatch(channel ->
                Contracts.SPOT_CHANNEL.equals(channel.channelName())),
            "MON-A1 channel snapshot is missing");
        MonitoringScenarioContext.ensure(
            current.channels().stream().anyMatch(channel ->
                Contracts.SPOT_CHANNEL.equals(channel.channelName())
                    && channel.readyMemberCount() >= 2
                    && channel.selectable()),
            "MON-A1 ready channel membership is incomplete");
        MonitoringScenarioContext.ensure(
            baseline.descriptorSources() != null
                && baseline.peers() != null
                && baseline.channels() != null
                && baseline.locationState() != null
                && baseline.drainState() != null,
            "MON-A1 aggregate snapshot fields are incomplete");
        MonitoringScenarioContext.ensure(
            baseline.sequence() < current.sequence(),
            "MON-A1 first snapshot changed after the second read");
        context.waitForEvidenceAfter(
            context.serviceEndpoint(),
            evidenceBaseline,
            "route-mesh-runtime",
            "zlink.runtime.mesh_node.peer_changed");

        System.out.println("scenario MON-A1 passed");
    }
}
