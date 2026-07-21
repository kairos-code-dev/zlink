package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

public final class MonC1DispatchFailureScenario {
    private MonC1DispatchFailureScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String serviceA = context.serviceEndpoint();
        String serviceB = context.serviceBEndpoint();
        String blockedSpot = "monitoring-room-svc-a";
        String dropSpot = "monitoring-c1-drop";
        context.observer(serviceA, "start");
        context.post(serviceA, "/runtime/multicast/create?rid=" + dropSpot);
        context.post(serviceA, "/runtime/multicast/block?rid=" + blockedSpot);
        context.post(serviceA, "/runtime/multicast/block?rid=" + dropSpot);
        try {
            int evidenceStart = context.evidenceEntryCount(serviceA);
            context.publish(serviceA, "c1-gate", 1);
            context.waitForEvidenceAfter(
                serviceA,
                evidenceStart,
                "multicast",
                blockedSpot,
                "received");

            Contracts.RuntimeSnapshot blocked = context.awaitRuntimeSnapshot(
                serviceA,
                snapshot -> snapshot.applicationClaimActive()
                    && snapshot.infrastructureClaimActive(),
                "MON-C1 did not keep application and infrastructure claims distinct");
            context.post(serviceA, "/runtime/weight/zero");
            Contracts.WorkRes terminal =
                context.runtimeRequest(serviceB, "mon-c1-terminal");
            MonitoringScenarioContext.ensure(
                "svc-b".equals(terminal.providerRid())
                    && "work:mon-c1-terminal".equals(terminal.value()),
                "MON-C1 separate request did not complete while application was blocked");
            context.post(serviceA, "/runtime/weight/restore");

            for (int cycle = 0; cycle < 8; cycle++) {
                context.post(serviceB, "/runtime/weight/zero");
                context.post(serviceB, "/runtime/weight/restore");
            }
            Contracts.PublishOutcome pressure =
                context.publish(serviceA, "mon-c1-pressure", 10000, true);
            MonitoringScenarioContext.ensure(
                pressure.droppedLocal() > 0,
                "MON-C1 did not create cumulative local drop pressure");
            Contracts.RuntimeSnapshot active = context.awaitRuntimeSnapshot(
                serviceA,
                snapshot -> snapshot.multicastDropped() > blocked.multicastDropped()
                    && snapshot.applicationClaimActive()
                    && snapshot.infrastructureClaimActive(),
                "MON-C1 snapshot lost claim or multicast progress");

            Contracts.ObserverIsolationStatus normal = context.awaitObserver(
                serviceA,
                status -> status.normalEventCount() >= 2
                    && status.normalLatestSequence() > 0,
                "MON-C1 normal observer did not progress");
            MonitoringScenarioContext.ensure(
                !normal.slowFailed(),
                "MON-C1 slow observer failed before release");
            context.observer(serviceA, "release");
            Contracts.ObserverIsolationStatus isolated = context.awaitObserver(
                serviceA,
                status -> status.slowFailed() && status.slowLatestSequence() > 0,
                "MON-C1 slow observer failure was not isolated");
            Contracts.RuntimeSnapshot resynced = context.runtimeSnapshot(serviceA);
            MonitoringScenarioContext.ensure(
                resynced.sequence() >= isolated.slowLatestSequence()
                    && resynced.multicastDropped() >= active.multicastDropped(),
                "MON-C1 snapshot resync lost current multicast state");
        } finally {
            context.postBestEffort(serviceA, "/runtime/weight/restore");
            context.postBestEffort(
                serviceA, "/runtime/multicast/release?rid=" + blockedSpot);
            context.postBestEffort(
                serviceA, "/runtime/multicast/release?rid=" + dropSpot);
        }
        Contracts.WorkRes followUp =
            context.runtimeRequest(serviceB, "mon-c1-recovery");
        MonitoringScenarioContext.ensure(
            "work:mon-c1-recovery".equals(followUp.value()),
            "MON-C1 messaging stopped after observer failure");
        System.out.println("scenario MON-C1 passed");
    }
}
